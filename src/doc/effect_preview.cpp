// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/effect_preview.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
#include "raster/effects.hpp"
#include "raster/transform.hpp"

#include <algorithm>
#include <cstring>
#include <utility>
#include <vector>

namespace lundukepaint {

EffectPreview::EffectPreview(Document& document) : document_(&document) {
  layer_index_ = document.layers().active_index();
  if (layer_index_ >= 0 && layer_index_ < document.layers().count()) {
    snapshot_ = document.layers().at(layer_index_).clone();
  }
}

Layer* EffectPreview::target() {
  if (document_ == nullptr || snapshot_ == nullptr) {
    return nullptr;
  }
  if (layer_index_ < 0 || layer_index_ >= document_->layers().count()) {
    return nullptr;
  }
  Layer& layer = document_->layers().at(layer_index_);
  if (layer.width() != snapshot_->width() || layer.height() != snapshot_->height()) {
    return nullptr;
  }
  return &layer;
}

Rect EffectPreview::render(Layer& out, const EffectFn& fn) const {
  out.copy_from(*snapshot_);
  const Rect full{0, 0, out.width(), out.height()};
  const Selection& sel = document_->selection();
  // Run on the whole layer, then put back every pixel the mask does not
  // cover. A cropped tile made Trace Edges outline the selection (neighbors
  // past the edge read as paper) and restarted halftone, scanline, and
  // diffuse patterns at each selection's corner. The bounds are read now,
  // so Repeat Last Effect follows the current selection.
  Rect bounds = full;
  bool partial = false;
  if (!sel.empty() && !sel.inverted()) {
    const Rect canvas_sel = sel.bounds();
    const Rect local =
        rect_intersect(Rect{canvas_sel.x - snapshot_->offset_x(), canvas_sel.y - snapshot_->offset_y(),
                            canvas_sel.w, canvas_sel.h},
                       full);
    if (local.empty()) {
      return {};
    }
    partial = local.w != full.w || local.h != full.h;
    bounds = local;
  }
  // Pattern phase is the canvas coordinate of layer pixel (0, 0). A layer at
  // the origin (the usual case, and every full-canvas call) keeps phase 0,
  // so those pixels do not move. A partial selection on a shifted layer
  // shares one canvas phase instead of restarting at its own corner.
  struct PhaseScope {
    bool on = false;
    explicit PhaseScope(bool enable, int x, int y) : on(enable) {
      if (on) {
        set_pattern_phase(x, y);
      }
    }
    ~PhaseScope() {
      if (on) {
        set_pattern_phase(0, 0);
      }
    }
    PhaseScope(const PhaseScope&) = delete;
    PhaseScope& operator=(const PhaseScope&) = delete;
  } phase(partial, snapshot_->offset_x(), snapshot_->offset_y());
  fn(out.pixels(), out.width(), out.height(), out.stride());
  if (!sel.empty()) {
    clip_rect_to_selection(out, *snapshot_, full, sel);
  }
  return bounds;
}

Rect canvas_bounds_of(const Layer& layer, Rect local) {
  if (local.empty()) {
    return {};
  }
  local.x += layer.offset_x();
  local.y += layer.offset_y();
  return local;
}

Rect EffectPreview::preview(const EffectFn& fn) {
  Layer* layer = target();
  if (layer == nullptr || !fn) {
    return {};
  }
  // Full resolution, same path as commit(). A downscaled nearest-neighbor
  // proxy does not match blur, sharpen, or emboss.
  Layer scratch(snapshot_->width(), snapshot_->height(), Color::transparent(), "preview");
  const Rect bounds = render(scratch, fn);
  layer->copy_from(scratch);
  previewing_ = true;
  document_->notify_invalidated(canvas_bounds_of(*layer, bounds));
  return bounds;
}

bool EffectPreview::restore() {
  if (!previewing_) {
    return false;
  }
  Layer* layer = target();
  previewing_ = false;
  if (layer == nullptr) {
    return false;
  }
  layer->copy_from(*snapshot_);
  document_->notify_invalidated(
      canvas_bounds_of(*layer, Rect{0, 0, layer->width(), layer->height()}));
  return true;
}

bool EffectPreview::commit(const std::string& name, const EffectFn& fn) {
  Layer* layer = target();
  if (layer == nullptr || !fn) {
    return false;
  }
  // Throw the live-preview pixels away so the effect is applied exactly once,
  // from the pristine snapshot, as one history entry.
  layer->copy_from(*snapshot_);
  previewing_ = false;

  Layer after(snapshot_->width(), snapshot_->height(), Color::transparent(), "after");
  const Rect bounds = render(after, fn);
  auto command = PixelPatchCommand::from_layers(*snapshot_, after, bounds,
                                                name.empty() ? std::string("Adjust") : name,
                                                layer_index_);
  if (!command || command->empty()) {
    document_->notify_invalidated(Rect{0, 0, layer->width(), layer->height()});
    return false;
  }
  document_->commit(std::move(command));
  return true;
}

}  // namespace lundukepaint
