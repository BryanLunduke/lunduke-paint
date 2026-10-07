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
    host_->show_status_hint("Layer is locked");
    return false;
  }
  return true;
}

}  // namespace lundukepaint
