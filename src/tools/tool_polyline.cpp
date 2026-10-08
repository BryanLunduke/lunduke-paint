// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/rail_options.hpp"
#include "tools/shape_options.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/shapes.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>
#include <vector>

namespace lundukepaint {

class PolylineTool : public Tool {
public:
  const char* id() const override { return "polyline"; }
  const char* name() const override { return "Polyline"; }
  char shortcut() const override { return 'N'; }
  const char* hint() const override {
    return "Polyline: click vertices; Enter or double-click finishes; Esc cancels";
  }
  bool is_stroking() const override { return !xs_.empty(); }
  bool has_uncommitted_preview() const override { return xs_.size() >= 2; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent /*event*/) override {}
  void on_cancel() override;
  void on_double_click(CanvasEvent event) override;
  bool on_commit() override;

private:
  void add_point(int x, int y, bool constrain);
  void preview();
  bool finish();
  void clear_preview();
  void sync_overlay();

  std::vector<int> xs_;
  std::vector<int> ys_;
  int hover_x_ = 0;
  int hover_y_ = 0;
  unsigned button_ = 1;
  bool antialias_ = false;
  const char* family_ = "polyline";
  Gtk::CheckButton* aa_button_ = nullptr;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* PolylineTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    prepare_rail_box(*options_);
    auto* aa = Gtk::manage(new Gtk::CheckButton("Anti-alias"));
    aa_button_ = aa;
    aa->set_tooltip_text("Smooth the polyline edges");
    configure_rail_check(*aa);
    aa->signal_toggled().connect([this, aa]() {
      antialias_ = aa->get_active();
      shape_family_options(family_).antialias = antialias_;
    });
    options_->pack_start(*aa, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  antialias_ = shape_family_options(family_).antialias;
  if (aa_button_ != nullptr && aa_button_->get_active() != antialias_) {
    aa_button_->set_active(antialias_);
  }
  return options_.get();
}

void PolylineTool::sync_overlay() {
  if (host_ == nullptr) {
    return;
  }
  host_->document().set_unsaved_overlay(xs_.size() >= 2);
}

void PolylineTool::add_point(int x, int y, bool constrain) {
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

void PolylineTool::preview() {
  if (host_ == nullptr || xs_.empty()) {
    return;
  }
  Document& doc = host_->document();
  const Layer& active = preview_layer_ref();
  Layer& tool = doc.layers().tool_layer();
  const Rect previous = dirty_;
  restore_shape_preview(tool, active, previous);
  std::vector<int> xs = xs_;
  std::vector<int> ys = ys_;
  if (hover_x_ != xs.back() || hover_y_ != ys.back()) {
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
  draw_polyline(tool.pixels(), tool.width(), tool.height(), tool.stride(), xs.data(), ys.data(),
                static_cast<int>(xs.size()), stroke_px(), stroke_color(button_), antialias_,
                &dirty_);
  clip_rect_to_selection(tool, active, dirty_, doc.selection());
  host_->invalidate_canvas(layer_dirty_to_canvas(active, rect_union(previous, dirty_)));
  sync_overlay();
}

void PolylineTool::clear_preview() {
  if (host_ != nullptr) {
    const Layer& shown = preview_layer_ref();
    host_->document().layers().clear_tool_layer();
    host_->invalidate_canvas(layer_dirty_to_canvas(shown, dirty_));
  }
  xs_.clear();
  ys_.clear();
  dirty_ = {};
  preview_layer_ = -1;
  sync_overlay();
}

bool PolylineTool::finish() {
  if (host_ == nullptr || xs_.size() < 2) {
    clear_preview();
    return true;
  }
  antialias_ = shape_family_options(family_).antialias;
  preview();
  if (!commit_preview("Polyline", dirty_)) {
    return false;
  }
  xs_.clear();
  ys_.clear();
  dirty_ = {};
  sync_overlay();
  return true;
}

void PolylineTool::on_press(CanvasEvent event) {
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
    arm_preview_layer();
    button_ = event.button;
  }
  add_point(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
            (event.modifiers & Modifier::Shift) != 0);
  preview();
}

void PolylineTool::on_motion(CanvasEvent event) {
  if (xs_.empty()) {
    return;
  }
  hover_x_ = static_cast<int>(std::floor(event.x));
  hover_y_ = static_cast<int>(std::floor(event.y));
  if ((event.modifiers & Modifier::Shift) != 0) {
    constrain_line_45(xs_.back(), ys_.back(), &hover_x_, &hover_y_);
  }
  preview();
}

void PolylineTool::on_double_click(CanvasEvent event) {
  if (xs_.empty()) {
    return;
  }
  add_point(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
            (event.modifiers & Modifier::Shift) != 0);
  finish();
}

bool PolylineTool::on_commit() {
  if (xs_.empty()) {
    return false;
  }
  return finish();
}

void PolylineTool::on_cancel() {
  if (xs_.empty()) {
    return;
  }
  clear_preview();
}

Tool* create_polyline_tool() {
  return new PolylineTool();
}

}  // namespace lundukepaint
