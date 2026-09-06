// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

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
  void finish();
  void clear_preview();

  std::vector<int> xs_;
  std::vector<int> ys_;
  int hover_x_ = 0;
  int hover_y_ = 0;
  unsigned button_ = 1;
  int thickness_ = 1;
  bool antialias_ = false;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* PolylineTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* tlabel = Gtk::manage(new Gtk::Label("Thickness"));
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(1, 64);
    spin->set_increments(1, 4);
    spin->set_digits(0);
    spin->set_value(thickness_);
    spin->signal_value_changed().connect([this, spin]() { thickness_ = spin->get_value_as_int(); });
    auto* aa = Gtk::manage(new Gtk::CheckButton("Anti-alias"));
    aa->set_active(antialias_);
    aa->signal_toggled().connect([this, aa]() { antialias_ = aa->get_active(); });
    options_->pack_start(*tlabel, Gtk::PACK_SHRINK);
    options_->pack_start(*spin, Gtk::PACK_SHRINK);
    options_->pack_start(*aa, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
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
  Layer& tool = doc.layers().tool_layer();
  tool.copy_from(doc.layers().active_layer());
  std::vector<int> xs = xs_;
  std::vector<int> ys = ys_;
  if (hover_x_ != xs.back() || hover_y_ != ys.back()) {
    xs.push_back(hover_x_);
    ys.push_back(hover_y_);
  }
  dirty_ = {};
  draw_polyline(tool.pixels(), tool.width(), tool.height(), tool.stride(), xs.data(), ys.data(),
                static_cast<int>(xs.size()), thickness_, stroke_color(button_), antialias_,
                &dirty_);
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty_, doc.selection());
  host_->invalidate_canvas(dirty_);
}

void PolylineTool::clear_preview() {
  if (host_ != nullptr) {
    host_->document().layers().clear_tool_layer();
    host_->invalidate_canvas(dirty_);
  }
  xs_.clear();
  ys_.clear();
  dirty_ = {};
}

void PolylineTool::finish() {
  if (host_ == nullptr || xs_.size() < 2) {
    clear_preview();
    return;
  }
  preview();
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Polyline", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  xs_.clear();
  ys_.clear();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
  dirty_ = {};
}

void PolylineTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  if (xs_.empty()) {
    host_->document().commit_floating();
    host_->document().layers().copy_active_to_tool();
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
  finish();
  return true;
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
