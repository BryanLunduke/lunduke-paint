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

namespace lundukepaint {

class CurveTool : public Tool {
public:
  const char* id() const override { return "curve"; }
  const char* name() const override { return "Curve"; }
  char shortcut() const override { return 'V'; }
  const char* hint() const override {
    return "Curve: drag endpoints, then two handles (KolourPaint); Enter commits; Esc cancels";
  }
  bool is_stroking() const override { return phase_ != 0; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;
  bool on_commit() override;

private:
  void set_point(int which, int x, int y, bool constrain);
  void preview();
  void finish();
  void reset();

  int phase_ = 0;  // 0 idle, 1 endpoints, 2 handle1, 3 handle2
  int x0_ = 0;
  int y0_ = 0;
  int x1_ = 0;
  int y1_ = 0;
  int x2_ = 0;
  int y2_ = 0;
  int x3_ = 0;
  int y3_ = 0;
  unsigned button_ = 1;
  int thickness_ = 1;
  bool antialias_ = false;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* CurveTool::options_widget() {
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

void CurveTool::set_point(int which, int x, int y, bool constrain) {
  if (which == 3 && constrain) {
    constrain_line_45(x0_, y0_, &x, &y);
  }
  if (which == 1) {
    x1_ = x;
    y1_ = y;
  } else if (which == 2) {
    x2_ = x;
    y2_ = y;
  } else if (which == 3) {
    x3_ = x;
    y3_ = y;
  }
}

void CurveTool::preview() {
  if (host_ == nullptr || phase_ == 0) {
    return;
  }
  Document& doc = host_->document();
  Layer& tool = doc.layers().tool_layer();
  tool.copy_from(doc.layers().active_layer());
  dirty_ = {};
  if (phase_ == 1) {
    draw_line(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0_, y0_, x3_, y3_,
              thickness_, stroke_color(button_), antialias_, &dirty_);
  } else {
    draw_cubic_bezier(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0_, y0_, x1_, y1_,
                      x2_, y2_, x3_, y3_, thickness_, stroke_color(button_), antialias_, &dirty_);
  }
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty_, doc.selection());
  host_->invalidate_canvas(dirty_);
}

void CurveTool::reset() {
  phase_ = 0;
  dirty_ = {};
}

void CurveTool::finish() {
  if (host_ == nullptr) {
    reset();
    return;
  }
  if (phase_ == 1) {
    x1_ = x0_ + (x3_ - x0_) / 3;
    y1_ = y0_ + (y3_ - y0_) / 3;
    x2_ = x0_ + 2 * (x3_ - x0_) / 3;
    y2_ = y0_ + 2 * (y3_ - y0_) / 3;
  } else if (phase_ == 2) {
    x2_ = x0_ + 2 * (x3_ - x0_) / 3;
    y2_ = y0_ + 2 * (y3_ - y0_) / 3;
  }
  preview();
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Curve", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  reset();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
}

void CurveTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  if (phase_ == 0) {
    host_->document().commit_floating();
    host_->document().layers().copy_active_to_tool();
    button_ = event.button;
    x0_ = x1_ = x2_ = x3_ = x;
    y0_ = y1_ = y2_ = y3_ = y;
    phase_ = 1;
    preview();
    return;
  }
  if (phase_ == 2) {
    set_point(1, x, y, false);
    preview();
    return;
  }
  if (phase_ == 3) {
    set_point(2, x, y, false);
    preview();
  }
}

void CurveTool::on_motion(CanvasEvent event) {
  if (phase_ == 0) {
    return;
  }
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  const bool shift = (event.modifiers & Modifier::Shift) != 0;
  if (phase_ == 1) {
    set_point(3, x, y, shift);
  } else if (phase_ == 2) {
    set_point(1, x, y, false);
  } else if (phase_ == 3) {
    set_point(2, x, y, false);
  }
  preview();
}

void CurveTool::on_release(CanvasEvent event) {
  if (phase_ == 0) {
    return;
  }
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  const bool shift = (event.modifiers & Modifier::Shift) != 0;
  if (phase_ == 1) {
    set_point(3, x, y, shift);
    x1_ = x0_ + (x3_ - x0_) / 3;
    y1_ = y0_ + (y3_ - y0_) / 3;
    x2_ = x0_ + 2 * (x3_ - x0_) / 3;
    y2_ = y0_ + 2 * (y3_ - y0_) / 3;
    phase_ = 2;
    preview();
    return;
  }
  if (phase_ == 2) {
    set_point(1, x, y, false);
    phase_ = 3;
    preview();
    return;
  }
  if (phase_ == 3) {
    set_point(2, x, y, false);
    finish();
  }
}

bool CurveTool::on_commit() {
  if (phase_ == 0) {
    return false;
  }
  finish();
  return true;
}

void CurveTool::on_cancel() {
  if (phase_ == 0) {
    return;
  }
  if (host_ != nullptr) {
    host_->document().layers().clear_tool_layer();
    host_->invalidate_canvas(dirty_);
  }
  reset();
}

Tool* create_curve_tool() {
  return new CurveTool();
}

}  // namespace lundukepaint
