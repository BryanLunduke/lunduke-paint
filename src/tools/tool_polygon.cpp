// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/rail_options.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/shapes.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>
#include <vector>

namespace lundukepaint {

class PolygonTool : public Tool {
public:
  explicit PolygonTool(ShapeFillMode mode = ShapeFillMode::Stroke, const char* tool_id = "polygon",
                       const char* tool_name = "Polygon")
      : fill_mode_(mode), id_(tool_id), name_(tool_name) {}
  const char* id() const override { return id_; }
  const char* name() const override { return name_; }
  char shortcut() const override { return fill_mode_ == ShapeFillMode::Fill ? 'Q' : 'G'; }
  const char* hint() const override {
    return "Polygon: click vertices; Enter or double-click closes; Esc cancels";
  }
  bool is_stroking() const override { return !xs_.empty(); }
  bool has_uncommitted_preview() const override { return xs_.size() >= 2; }
  Gtk::Widget* options_widget() override;
  void set_shape_fill_mode(ShapeFillMode mode) override { fill_mode_ = mode; }

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent /*event*/) override {}
  void on_cancel() override;
  void on_double_click(CanvasEvent event) override;
  bool on_commit() override;

private:
  void add_point(int x, int y, bool constrain);
  void preview(bool closed);
  void finish();
  void clear_preview();
  void sync_overlay();

  std::vector<int> xs_;
  std::vector<int> ys_;
  int hover_x_ = 0;
  int hover_y_ = 0;
  unsigned button_ = 1;
  bool antialias_ = false;
  ShapeFillMode fill_mode_ = ShapeFillMode::Stroke;
  const char* id_ = "polygon";
  const char* name_ = "Polygon";
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* PolygonTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    prepare_rail_box(*options_);
    auto* aa = Gtk::manage(new Gtk::CheckButton("Anti-alias"));
    aa->set_active(antialias_);
    aa->set_tooltip_text("Smooth the polygon edges");
    configure_rail_check(*aa);
    aa->signal_toggled().connect([this, aa]() { antialias_ = aa->get_active(); });
    options_->pack_start(*aa, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
}

void PolygonTool::sync_overlay() {
  if (host_ == nullptr) {
    return;
  }
  host_->document().set_unsaved_overlay(xs_.size() >= 2);
}

void PolygonTool::add_point(int x, int y, bool constrain) {
  if (constrain && !xs_.empty()) {
    constrain_line_45(xs_.back(), ys_.back(), &x, &y);
  }
  if (!xs_.empty() && xs_.back() == x && ys_.back() == y) {
    return;
  }
  xs_.push_back(x);
  ys_.push_back(y);
  hover_x_ = x;
  hover_y_ = y;
}

void PolygonTool::preview(bool closed) {
  if (host_ == nullptr || xs_.empty()) {
    return;
  }
  Document& doc = host_->document();
  const Layer& active = doc.layers().active_layer();
  Layer& tool = doc.layers().tool_layer();
  const Rect previous = dirty_;
  restore_shape_preview(tool, active, previous);
  std::vector<int> xs = xs_;
  std::vector<int> ys = ys_;
  if (!closed && (hover_x_ != xs.back() || hover_y_ != ys.back())) {
    xs.push_back(hover_x_);
    ys.push_back(hover_y_);
  }
  const int ox = active.offset_x();
  const int oy = active.offset_y();
  for (int& px : xs) {
    px -= ox;
  }
  for (int& py : ys) {
    py -= oy;
  }
  dirty_ = {};
  if (closed && xs.size() >= 3) {
    draw_polygon(tool.pixels(), tool.width(), tool.height(), tool.stride(), xs.data(), ys.data(),
                 static_cast<int>(xs.size()), stroke_px(), stroke_color(button_), fill_mode_,
                 antialias_, &dirty_);
  } else {
    draw_polyline(tool.pixels(), tool.width(), tool.height(), tool.stride(), xs.data(), ys.data(),
                  static_cast<int>(xs.size()), stroke_px(), stroke_color(button_), antialias_,
                  &dirty_);
  }
  clip_rect_to_selection(tool, active, dirty_, doc.selection());
  host_->invalidate_canvas(
      layer_dirty_to_canvas(host_->document().layers().active_layer(), rect_union(previous, dirty_)));
  sync_overlay();
}

void PolygonTool::clear_preview() {
  if (host_ != nullptr) {
    host_->document().layers().clear_tool_layer();
    host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  }
  xs_.clear();
  ys_.clear();
  dirty_ = {};
  sync_overlay();
}

void PolygonTool::finish() {
  if (host_ == nullptr || xs_.size() < 3) {
    clear_preview();
    return;
  }
  preview(true);
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Polygon", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  xs_.clear();
  ys_.clear();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  }
  dirty_ = {};
  sync_overlay();
}

void PolygonTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  if (xs_.empty()) {
    if (!commit_float_or_stop()) {
      return;
    }
    host_->document().layers().copy_active_to_tool();
    button_ = event.button;
  }
  add_point(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
            (event.modifiers & Modifier::Shift) != 0);
  preview(false);
}

void PolygonTool::on_motion(CanvasEvent event) {
  if (xs_.empty()) {
    return;
  }
  hover_x_ = static_cast<int>(std::floor(event.x));
  hover_y_ = static_cast<int>(std::floor(event.y));
  if ((event.modifiers & Modifier::Shift) != 0) {
    constrain_line_45(xs_.back(), ys_.back(), &hover_x_, &hover_y_);
  }
  preview(false);
}

void PolygonTool::on_double_click(CanvasEvent event) {
  if (xs_.empty()) {
    return;
  }
  add_point(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
            (event.modifiers & Modifier::Shift) != 0);
  finish();
}

bool PolygonTool::on_commit() {
  if (xs_.empty()) {
    return false;
  }
  finish();
  return true;
}

void PolygonTool::on_cancel() {
  if (xs_.empty()) {
    return;
  }
  clear_preview();
}

Tool* create_polygon_tool() {
  return new PolygonTool(ShapeFillMode::Stroke, "polygon", "Polygon");
}

Tool* create_polygon_fill_tool() {
  return new PolygonTool(ShapeFillMode::Fill, "polygon-fill", "Polygon fill");
}

}  // namespace lundukepaint
