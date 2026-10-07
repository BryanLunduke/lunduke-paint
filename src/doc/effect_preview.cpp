// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/effect_preview.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
#include "raster/transform.hpp"

#include <algorithm>
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
  fn(out.pixels(), out.width(), out.height(), out.stride());
  Rect bounds{0, 0, out.width(), out.height()};
  const Selection& sel = document_->selection();
  if (!sel.empty()) {
    clip_rect_to_selection(out, *snapshot_, bounds, sel);
    if (!sel.inverted()) {
      const Rect canvas_sel = sel.bounds();
      const Rect local{canvas_sel.x - snapshot_->offset_x(), canvas_sel.y - snapshot_->offset_y(),
                       canvas_sel.w, canvas_sel.h};
      bounds = rect_intersect(local, bounds);
    }
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
  Rect bounds{};
  if (snapshot_->width() > 2048 || snapshot_->height() > 2048) {
    int dw = snapshot_->width();
    int dh = snapshot_->height();
    constexpr int kPreviewMax = 1024;
    if (dw >= dh) {
      dh = std::max(1, dh * kPreviewMax / dw);
      dw = kPreviewMax;
    } else {
      dw = std::max(1, dw * kPreviewMax / dh);
      dh = kPreviewMax;
    }
    std::vector<std::uint8_t> small(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh) * 4, 0);
    scale_bilinear(snapshot_->pixels(), snapshot_->width(), snapshot_->height(), snapshot_->stride(),
                   small.data(), dw, dh, dw * 4);
    fn(small.data(), dw, dh, dw * 4);
    std::vector<std::uint8_t> up(static_cast<std::size_t>(snapshot_->width()) *
                                     static_cast<std::size_t>(snapshot_->height()) * 4,
                                 0);
    scale_nearest(small.data(), dw, dh, dw * 4, up.data(), snapshot_->width(), snapshot_->height(),
                  snapshot_->width() * 4);
    layer->set_pixels(snapshot_->width(), snapshot_->height(), up.data(), snapshot_->width() * 4);
    bounds = Rect{0, 0, snapshot_->width(), snapshot_->height()};
    const Selection& sel = document_->selection();
    if (!sel.empty()) {
      clip_rect_to_selection(*layer, *snapshot_, bounds, sel);
    }
  } else {
    Layer scratch(snapshot_->width(), snapshot_->height(), Color::transparent(), "preview");
    bounds = render(scratch, fn);
    layer->copy_from(scratch);
  }
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
