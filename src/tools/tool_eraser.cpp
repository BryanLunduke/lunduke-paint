// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/stroke.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <memory>

namespace lundukepaint {

class EraserTool : public Tool {
public:
  const char* id() const override { return "eraser"; }
  const char* name() const override { return "Eraser"; }
  char shortcut() const override { return 'A'; }
  const char* hint() const override { return "Eraser: paints the background color"; }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  void begin_stroke(CanvasEvent event);
  void stamp_to(double x, double y);
  void finish_stroke();
  Color erase_color() const;

  bool drawing_ = false;
  double last_x_ = 0;
  double last_y_ = 0;
  int size_ = 8;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* EraserTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* label = Gtk::manage(new Gtk::Label("Size"));
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(1, 64);
    spin->set_increments(1, 4);
    spin->set_digits(0);
    spin->set_value(size_);
    spin->signal_value_changed().connect([this, spin]() { size_ = spin->get_value_as_int(); });
    options_->pack_start(*label, Gtk::PACK_SHRINK);
    options_->pack_start(*spin, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
}

Color EraserTool::erase_color() const {
  if (host_ == nullptr) {
    return Color::white();
  }
  return host_->document().background();
}

void EraserTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  begin_stroke(event);
}

void EraserTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    stamp_to(event.x, event.y);
  }
}

void EraserTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(event.x, event.y);
  finish_stroke();
}

void EraserTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(dirty_);
  dirty_ = {};
}

void EraserTool::begin_stroke(CanvasEvent event) {
  if (host_ == nullptr) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  host_->document().commit_floating();
  drawing_ = true;
  last_x_ = event.x;
  last_y_ = event.y;
  dirty_ = {};
  host_->document().layers().copy_active_to_tool();
  stamp_to(last_x_, last_y_);
}

void EraserTool::stamp_to(double x, double y) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Layer& tool = host_->document().layers().tool_layer();
  stroke_brush(tool.pixels(), tool.width(), tool.height(), tool.stride(), last_x_, last_y_, x, y,
               size_, erase_color(), false, &dirty_);
  clip_rect_to_selection(tool, host_->document().layers().active_layer(), dirty_,
                         host_->document().selection());
  last_x_ = x;
  last_y_ = y;
  host_->invalidate_canvas(dirty_);
}

void EraserTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Eraser", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
  dirty_ = {};
}

Tool* create_eraser_tool() {
  return new EraserTool();
}

}  // namespace lundukepaint
