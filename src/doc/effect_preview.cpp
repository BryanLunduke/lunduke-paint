// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/effect_preview.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
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
  Rect bounds{0, 0, out.width(), out.height()};
  const Selection& sel = document_->selection();
  // A small selection used to run the effect on the whole layer and then
  // throw the outside away. Run it on the selection bounds instead. The
  // bounds are read now, so Repeat Last Effect follows the current selection.
  if (!sel.empty() && !sel.inverted()) {
    const Rect canvas_sel = sel.bounds();
    const Rect local =
        rect_intersect(Rect{canvas_sel.x - snapshot_->offset_x(), canvas_sel.y - snapshot_->offset_y(),
                            canvas_sel.w, canvas_sel.h},
                       bounds);
    if (local.empty()) {
      return {};
    }
    if (local.w != bounds.w || local.h != bounds.h) {
      const int tile_stride = local.w * 4;
      std::vector<std::uint8_t> tile(static_cast<std::size_t>(tile_stride) *
                                     static_cast<std::size_t>(local.h));
      for (int y = 0; y < local.h; ++y) {
        const std::uint8_t* src = out.pixels() +
                                  static_cast<std::size_t>(local.y + y) * static_cast<std::size_t>(out.stride()) +
                                  static_cast<std::size_t>(local.x) * 4;
        std::memcpy(tile.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(tile_stride), src,
                    static_cast<std::size_t>(tile_stride));
      }
      fn(tile.data(), local.w, local.h, tile_stride);
      for (int y = 0; y < local.h; ++y) {
        std::uint8_t* dst = out.pixels() +
                            static_cast<std::size_t>(local.y + y) * static_cast<std::size_t>(out.stride()) +
                            static_cast<std::size_t>(local.x) * 4;
        std::memcpy(dst, tile.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(tile_stride),
                    static_cast<std::size_t>(tile_stride));
      }
      bounds = local;
    } else {
      fn(out.pixels(), out.width(), out.height(), out.stride());
    }
  } else {
    fn(out.pixels(), out.width(), out.height(), out.stride());
  }
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
