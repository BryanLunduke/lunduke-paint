// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"

#include <cstring>

namespace lundukepaint {

Color Tool::stroke_color(unsigned button) const {
  if (host_ == nullptr) {
    return Color::black();
  }
  if (button == 3) {
    return host_->document().background();
  }
  return host_->document().foreground();
}

void restore_shape_preview(Layer& tool, const Layer& active, Rect previous) {
  if (!previous.empty()) {
    tool.copy_rect_from(active, previous);
  }
}

bool Tool::ensure_editable() {
  if (host_ == nullptr) {
    return false;
  }
  if (host_->document().layers().active_layer().locked()) {
    host_->document().notify_blocked("Layer is locked");
    return false;
  }
  return true;
}

int Tool::stroke_px() const {
  if (host_ == nullptr) {
    return 1;
  }
  const int size = host_->stroke_size();
  return size < 1 ? 1 : size;
}

bool Tool::commit_float_or_stop() {
  if (host_ == nullptr) {
    return false;
  }
  if (host_->document().try_commit_floating()) {
    return true;
  }
  host_->document().notify_blocked("Unlock the layer to place the selection");
  return false;
}

void Tool::arm_preview_layer() {
  if (host_ == nullptr) {
    return;
  }
  preview_layer_ = host_->document().layers().active_index();
  host_->document().layers().copy_active_to_tool();
  host_->document().set_unsaved_overlay(true);
}

const Layer& Tool::preview_layer_ref() const {
  Document& doc = host_->document();
  if (preview_layer_ >= 0 && preview_layer_ < doc.layers().count()) {
    return doc.layers().at(preview_layer_);
  }
  return doc.layers().active_layer();
}

void Tool::clear_preview_overlay() {
  preview_layer_ = -1;
  if (host_ != nullptr && !host_->document().selection().floating()) {
    host_->document().set_unsaved_overlay(false);
  }
}

bool Tool::commit_preview(const char* name, Rect dirty) {
  if (host_ == nullptr) {
    preview_layer_ = -1;
    return false;
  }
  Document& doc = host_->document();
  int index = preview_layer_;
  if (index < 0 || index >= doc.layers().count()) {
    index = doc.layers().active_index();
  }
  Layer& layer = doc.layers().at(index);
  if (layer.locked()) {
    doc.notify_blocked("Unlock the layer to place the shape");
    return false;
  }
  auto cmd = PixelPatchCommand::from_layers(layer, doc.layers().tool_layer(), dirty,
                                            name != nullptr ? name : "Shape", index);
  doc.layers().clear_tool_layer();
  preview_layer_ = -1;
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(layer_dirty_to_canvas(layer, dirty));
  }
  if (!doc.selection().floating()) {
    doc.set_unsaved_overlay(false);
  }
  return true;
}

bool Tool::paint_recovery_overlay(int layer_index, std::uint8_t* pixels, int width, int height,
                                 int stride) {
  if (host_ == nullptr || pixels == nullptr || width < 1 || height < 1 || stride < width * 4) {
    return false;
  }
  if (!is_stroking() || !uses_tool_layer()) {
    return false;
  }
  int index = preview_layer_;
  if (index < 0) {
    index = host_->document().layers().active_index();
  }
  if (layer_index != index) {
    return false;
  }
  const Layer& tool = host_->document().layers().tool_layer();
  if (tool.width() != width || tool.height() != height || tool.pixels() == nullptr) {
    return false;
  }
  bool any = false;
  for (int y = 0; y < height; ++y) {
    std::uint8_t* dst = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    const std::uint8_t* src =
        tool.pixels() + static_cast<std::size_t>(y) * static_cast<std::size_t>(tool.stride());
    for (int x = 0; x < width; ++x) {
      const std::uint8_t* s = src + static_cast<std::size_t>(x) * 4;
      std::uint8_t* d = dst + static_cast<std::size_t>(x) * 4;
      if (std::memcmp(s, d, 4) != 0) {
        std::memcpy(d, s, 4);
        any = true;
      }
    }
  }
  return any;
}

}  // namespace lundukepaint
