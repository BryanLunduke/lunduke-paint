// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/brush_tip.hpp"
#include "raster/custom_brush.hpp"
#include "raster/grid_snap.hpp"
#include "raster/stroke.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>

#include <cstring>
#include <cstdint>
#include <memory>
#include <vector>

#include <cmath>

namespace lundukepaint {

class BrushTool : public Tool {
public:
  const char* id() const override { return "brush"; }
  const char* name() const override { return "Brush"; }
  char shortcut() const override { return 'B'; }
  const char* hint() const override {
    return smear_ ? "Smear: drag to drag pixels with the tip; Shift keeps a straight line"
                  : "Brush: drag to paint; Shift keeps a straight line from the last point";
  }
  PointerOutlineInfo pointer_outline() const override {
    PointerOutlineInfo info;
    if (host_ != nullptr && host_->custom_brush() != nullptr && !host_->custom_brush()->empty()) {
      info.kind = PointerOutline::Custom;
      info.brush = host_->custom_brush();
      return info;
    }
    info.kind = PointerOutline::Tip;
    info.tip = host_ != nullptr ? host_->brush_tip() : 3;
    info.size = brush_tip_at(info.tip).size;
    if (host_ != nullptr && host_->brush_tip_px() > 0) {
      info.size = host_->brush_tip_px();
    }
    return info;
  }
  Gtk::Widget* options_widget() override;
  bool is_stroking() const override { return drawing_; }

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;
  bool on_commit() override;

private:
  void begin_stroke(CanvasEvent event);
  void stamp_to(double x, double y, bool shift);
  double snapped(double v) const;
  bool finish_stroke();

  bool drawing_ = false;
  double last_x_ = 0;
  double last_y_ = 0;
  bool shift_on_ = false;
  double anchor_x_ = 0;
  double anchor_y_ = 0;
  double rubber_x_ = 0;
  double rubber_y_ = 0;
  Rect shift_dirty_{};
  std::vector<std::uint8_t> shift_backup_;
  unsigned button_ = 1;
  Rect dirty_{};
  bool smear_ = false;
  Gtk::CheckButton* smear_button_ = nullptr;
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* BrushTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    auto* smear = Gtk::manage(new Gtk::CheckButton("Smear"));
    smear_button_ = smear;
    smear->set_tooltip_text("Deluxe Paint smear: the tip drags the pixels under it");
    smear->signal_toggled().connect([this, smear]() { smear_ = smear->get_active(); });
    options_->pack_start(*smear, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  if (smear_button_ != nullptr && smear_button_->get_active() != smear_) {
    smear_button_->set_active(smear_);
  }
  return options_.get();
}

double BrushTool::snapped(double v) const {
  if (host_ != nullptr && host_->grid_snap_enabled()) {
    return static_cast<double>(snap_canvas_coord(v, host_->grid_snap_spacing()));
  }
  return v;
}

void BrushTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  begin_stroke(event);
}

void BrushTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    stamp_to(snapped(event.x), snapped(event.y), (event.modifiers & Modifier::Shift) != 0);
  }
}

void BrushTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(snapped(event.x), snapped(event.y), (event.modifiers & Modifier::Shift) != 0);
  finish_stroke();
}

void BrushTool::on_cancel() {
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

bool BrushTool::on_commit() {
  if (!drawing_) {
    return false;
  }
  return finish_stroke();
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

void BrushTool::stamp_to(double x, double y, bool shift) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  const Layer& active = preview_layer_ref();
  Layer& tool = host_->document().layers().tool_layer();
  BrushTip tip = brush_tip_at(host_->brush_tip());
  if (host_->brush_tip_px() > 0) {
    tip.size = host_->brush_tip_px();
  }
  const CustomBrush* custom = host_->custom_brush();
  auto paint = [&](double x0, double y0, double x1, double y1, Rect& stamp) {
    if (smear_) {
      if (custom != nullptr && !custom->empty()) {
        smear_custom_brush(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0, y0, x1, y1,
                           *custom, &stamp);
      } else {
        smear_brush_tip(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0, y0, x1, y1, tip,
                        &stamp);
      }
      return;
    }
    if (custom != nullptr && !custom->empty()) {
      stroke_custom_brush(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0, y0, x1, y1,
                          *custom, &stamp);
      return;
    }
    stroke_brush_tip(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0, y0, x1, y1, tip,
                     stroke_color(button_), &stamp, &host_->active_pattern(), pattern_back(button_));
  };
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
  auto halo_of = [&](Rect stamp) {
    if (stamp.empty()) {
      return stamp;
    }
    stamp.x -= 1;
    stamp.y -= 1;
    stamp.w += 2;
    stamp.h += 2;
    return rect_intersect(stamp, Rect{0, 0, tool.width(), tool.height()});
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
    Rect stamp{};
    paint(anchor_x_ - active.offset_x(), anchor_y_ - active.offset_y(), x - active.offset_x(),
          y - active.offset_y(), stamp);
    shift_dirty_ = stamp;
    clip_rect_to_selection(tool, active, stamp, host_->document().selection());
    rubber_x_ = x;
    rubber_y_ = y;
    const Rect touched = rect_union(previous, stamp);
    dirty_ = rect_union(dirty_, touched);
    host_->invalidate_canvas(layer_dirty_to_canvas(active, halo_of(touched)));
    return;
  }
  if (shift_on_) {
    last_x_ = rubber_x_;
    last_y_ = rubber_y_;
    shift_on_ = false;
    shift_backup_.clear();
    shift_dirty_ = {};
  }
  Rect stamp{};
  paint(last_x_ - active.offset_x(), last_y_ - active.offset_y(), x - active.offset_x(),
        y - active.offset_y(), stamp);
  dirty_ = rect_union(dirty_, stamp);
  clip_rect_to_selection(tool, active, stamp, host_->document().selection());
  last_x_ = x;
  last_y_ = y;
  host_->invalidate_canvas(layer_dirty_to_canvas(active, halo_of(stamp)));
}

bool BrushTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return false;
  }
  if (!commit_preview("Brush stroke", dirty_)) {
    return false;
  }
  drawing_ = false;
  shift_on_ = false;
  shift_backup_.clear();
  dirty_ = {};
  return true;
}

Tool* create_brush_tool() {
  return new BrushTool();
}

}  // namespace lundukepaint
