// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/grid_snap.hpp"
#include "raster/stroke.hpp"

#include <cstring>

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

class PencilTool : public Tool {
public:
  const char* id() const override { return "pencil"; }
  const char* name() const override { return "Pencil"; }
  char shortcut() const override { return 'P'; }
  const char* hint() const override {
    return "Pencil: drag to draw; Shift keeps a straight line from the last point";
  }
  PointerOutlineInfo pointer_outline() const override {
    PointerOutlineInfo info;
    info.kind = PointerOutline::Square;
    info.size = stroke_px();
    return info;
  }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;
  bool on_commit() override;

private:
  void begin_stroke(CanvasEvent event);
  void stamp_to(int x, int y, bool shift);
  int snapped(double v) const;
  bool finish_stroke();

  bool drawing_ = false;
  int last_x_ = 0;
  int last_y_ = 0;
  bool shift_on_ = false;
  int anchor_x_ = 0;
  int anchor_y_ = 0;
  int rubber_x_ = 0;
  int rubber_y_ = 0;
  Rect shift_dirty_{};
  std::vector<std::uint8_t> shift_backup_;
  unsigned button_ = 1;
  Rect dirty_{};
};

Gtk::Widget* PencilTool::options_widget() {
  // The left-rail width picker is the pencil size.
  return nullptr;
}

int PencilTool::snapped(double v) const {
  if (host_ != nullptr && host_->grid_snap_enabled()) {
    return snap_canvas_coord(v, host_->grid_snap_spacing());
  }
  return static_cast<int>(std::floor(v));
}

void PencilTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  begin_stroke(event);
}

void PencilTool::on_motion(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(snapped(event.x), snapped(event.y), (event.modifiers & Modifier::Shift) != 0);
}

void PencilTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(snapped(event.x), snapped(event.y), (event.modifiers & Modifier::Shift) != 0);
  finish_stroke();
}

void PencilTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    clear_preview_overlay();
    return;
  }
  drawing_ = false;
  shift_on_ = false;
  shift_backup_.clear();
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(layer_dirty_to_canvas(preview_layer_ref(), dirty_));
  dirty_ = {};
  clear_preview_overlay();
}

bool PencilTool::on_commit() {
  if (!drawing_) {
    return false;
  }
  return finish_stroke();
}

void PencilTool::begin_stroke(CanvasEvent event) {
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
  shift_on_ = false;
  shift_backup_.clear();
  shift_dirty_ = {};
  button_ = event.button;
  last_x_ = snapped(event.x);
  last_y_ = snapped(event.y);
  dirty_ = {};
  arm_preview_layer();
  stamp_to(last_x_, last_y_, (event.modifiers & Modifier::Shift) != 0);
}

void PencilTool::stamp_to(int x, int y, bool shift) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  const Layer& active = preview_layer_ref();
  Layer& tool = host_->document().layers().tool_layer();
  const Color color = stroke_color(button_);
  const int size = host_->stroke_size();
  auto restore_rect = [&](Rect rect) {
    if (rect.empty() || shift_backup_.empty()) {
      return;
    }
    rect = rect_intersect(rect, Rect{0, 0, tool.width(), tool.height()});
    for (int row = rect.y; row < rect.y2(); ++row) {
      std::memcpy(tool.pixels() + static_cast<std::size_t>(row) * tool.stride() +
                      static_cast<std::size_t>(rect.x) * 4,
                  shift_backup_.data() + static_cast<std::size_t>(row) * tool.stride() +
                      static_cast<std::size_t>(rect.x) * 4,
                  static_cast<std::size_t>(rect.w) * 4);
    }
  };
  if (shift) {
    if (!shift_on_) {
      shift_on_ = true;
      anchor_x_ = last_x_;
      anchor_y_ = last_y_;
      shift_dirty_ = {};
      shift_backup_.assign(tool.pixels(),
                           tool.pixels() + static_cast<std::size_t>(tool.stride()) * tool.height());
    }
    const Rect previous = shift_dirty_;
    restore_rect(previous);
    Rect stroke{};
    stroke_pencil(tool.pixels(), tool.width(), tool.height(), tool.stride(),
                  anchor_x_ - active.offset_x(), anchor_y_ - active.offset_y(),
                  x - active.offset_x(), y - active.offset_y(), size, color, &stroke);
    shift_dirty_ = stroke;
    clip_rect_to_selection(tool, active, stroke, host_->document().selection());
    rubber_x_ = x;
    rubber_y_ = y;
    const Rect touched = rect_union(previous, stroke);
    dirty_ = rect_union(dirty_, touched);
    host_->invalidate_canvas(layer_dirty_to_canvas(active, touched));
    return;
  }
  if (shift_on_) {
    last_x_ = rubber_x_;
    last_y_ = rubber_y_;
    shift_on_ = false;
    shift_backup_.clear();
    shift_dirty_ = {};
  }
  const int x0 = last_x_ - active.offset_x();
  const int y0 = last_y_ - active.offset_y();
  const int x1 = x - active.offset_x();
  const int y1 = y - active.offset_y();
  Rect stroke{};
  stroke_pencil(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0, y0, x1, y1, size,
                color, &stroke);
  dirty_ = rect_union(dirty_, stroke);
  clip_rect_to_selection(tool, active, stroke, host_->document().selection());
  last_x_ = x;
  last_y_ = y;
  host_->invalidate_canvas(layer_dirty_to_canvas(active, stroke));
}

bool PencilTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return false;
  }
  if (!commit_preview("Pencil stroke", dirty_)) {
    return false;
  }
  drawing_ = false;
  shift_on_ = false;
  shift_backup_.clear();
  dirty_ = {};
  return true;
}

Tool* create_pencil_tool() {
  return new PencilTool();
}

}  // namespace lundukepaint
