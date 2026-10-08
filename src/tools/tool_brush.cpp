// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/brush_tip.hpp"
#include "raster/stroke.hpp"

#include <cmath>

namespace lundukepaint {

class BrushTool : public Tool {
public:
  const char* id() const override { return "brush"; }
  const char* name() const override { return "Brush"; }
  char shortcut() const override { return 'B'; }
  const char* hint() const override { return "Brush: drag to paint; tip from left-rail picker"; }
  bool is_stroking() const override { return drawing_; }
  // Options live in the MacPaint left-rail tip picker (0.5-4).
  Gtk::Widget* options_widget() override { return nullptr; }

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  void begin_stroke(CanvasEvent event);
  void stamp_to(double x, double y);
  void finish_stroke();

  bool drawing_ = false;
  double last_x_ = 0;
  double last_y_ = 0;
  unsigned button_ = 1;
  Rect dirty_{};
};

void BrushTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  begin_stroke(event);
}

void BrushTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    stamp_to(event.x, event.y);
  }
}

void BrushTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(event.x, event.y);
  finish_stroke();
}

void BrushTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  dirty_ = {};
}

void BrushTool::begin_stroke(CanvasEvent event) {
  if (host_ == nullptr) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  if (!commit_float_or_stop()) {
    return;
  }
  drawing_ = true;
  button_ = event.button;
  last_x_ = event.x;
  last_y_ = event.y;
  dirty_ = {};
  host_->document().layers().copy_active_to_tool();
  stamp_to(last_x_, last_y_);
}

void BrushTool::stamp_to(double x, double y) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  const Layer& active = host_->document().layers().active_layer();
  Layer& tool = host_->document().layers().tool_layer();
  const BrushTip tip = brush_tip_at(host_->brush_tip());
  const double x0 = last_x_ - active.offset_x();
  const double y0 = last_y_ - active.offset_y();
  const double x1 = x - active.offset_x();
  const double y1 = y - active.offset_y();
  Rect stamp{};
  stroke_brush_tip(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0, y0, x1, y1, tip,
                   stroke_color(button_), &stamp);
  dirty_ = rect_union(dirty_, stamp);
  clip_rect_to_selection(tool, active, stamp, host_->document().selection());
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

void BrushTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Brush stroke", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  }
  dirty_ = {};
}

Tool* create_brush_tool() {
  return new BrushTool();
}

}  // namespace lundukepaint
