// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/stroke.hpp"

#include <cstdint>

namespace lundukepaint {

class SprayTool : public Tool {
public:
  const char* id() const override { return "spray"; }
  const char* name() const override { return "Spraycan"; }
  char shortcut() const override { return 'Y'; }
  const char* hint() const override { return "Spraycan: drag; right uses BG"; }
  bool is_stroking() const override { return drawing_; }
  // Options live in the MacPaint left-rail spray picker (0.5-4).
  Gtk::Widget* options_widget() override { return nullptr; }

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;
  bool on_commit() override;

private:
  void begin_stroke(CanvasEvent event);
  void stamp_to(double x, double y);
  bool finish_stroke();

  bool drawing_ = false;
  unsigned button_ = 1;
  int radius_ = 16;
  // Left rail sets radius only. Density stays at this constant.
  int density_ = 40;
  std::uint32_t rng_ = 0xA5A5A5A5u;
  double last_x_ = 0;
  double last_y_ = 0;
  Rect dirty_{};
};

void SprayTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  begin_stroke(event);
}

void SprayTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    stamp_to(event.x, event.y);
  }
}

void SprayTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(event.x, event.y);
  finish_stroke();
}

void SprayTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    clear_preview_overlay();
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(layer_dirty_to_canvas(preview_layer_ref(), dirty_));
  dirty_ = {};
  clear_preview_overlay();
}

bool SprayTool::on_commit() {
  if (!drawing_) {
    return false;
  }
  return finish_stroke();
}

void SprayTool::begin_stroke(CanvasEvent event) {
  if (host_ == nullptr || !ensure_editable()) {
    return;
  }
  if (!commit_float_or_stop()) {
    return;
  }
  drawing_ = true;
  button_ = event.button;
  dirty_ = {};
  last_x_ = event.x;
  last_y_ = event.y;
  arm_preview_layer();
  stamp_to(event.x, event.y);
}

void SprayTool::stamp_to(double x, double y) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Document& doc = host_->document();
  const Layer& active = preview_layer_ref();
  Layer& tool = doc.layers().tool_layer();
  const int radius = host_ != nullptr ? host_->spray_radius() : radius_;
  Rect stamp{};
  stroke_spray(tool.pixels(), tool.width(), tool.height(), tool.stride(),
               last_x_ - active.offset_x(), last_y_ - active.offset_y(), x - active.offset_x(),
               y - active.offset_y(), radius, density_, stroke_color(button_), &rng_, &stamp);
  dirty_ = rect_union(dirty_, stamp);
  clip_rect_to_selection(tool, active, stamp, doc.selection());
  last_x_ = x;
  last_y_ = y;
  Rect halo = stamp;
  if (!halo.empty()) {
    halo.x -= 1;
    halo.y -= 1;
    halo.w += 2;
    halo.h += 2;
    halo = rect_intersect(halo, Rect{0, 0, tool.width(), tool.height()});
  }
  host_->invalidate_canvas(layer_dirty_to_canvas(active, halo));
}

bool SprayTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return false;
  }
  if (!commit_preview("Spray", dirty_)) {
    return false;
  }
  drawing_ = false;
  dirty_ = {};
  return true;
}

Tool* create_spray_tool() {
  return new SprayTool();
}

}  // namespace lundukepaint
