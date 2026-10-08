// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/document.hpp"
#include "doc/layer.hpp"

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

}  // namespace lundukepaint
