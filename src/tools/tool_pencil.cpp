// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/stroke.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>
#include <string>

namespace lundukepaint {

class PencilTool : public Tool {
public:
  const char* id() const override { return "pencil"; }
  const char* name() const override { return "Pencil"; }
  char shortcut() const override { return 'P'; }
  const char* hint() const override { return "Pencil: drag to draw hard pixels"; }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  void begin_stroke(CanvasEvent event);
  void stamp_to(int x, int y);
  void finish_stroke();

  bool drawing_ = false;
  int last_x_ = 0;
  int last_y_ = 0;
  unsigned button_ = 1;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
  Gtk::SpinButton* size_spin_{nullptr};
};

Gtk::Widget* PencilTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* label = Gtk::manage(new Gtk::Label("Size"));
    size_spin_ = Gtk::manage(new Gtk::SpinButton());
    size_spin_->set_range(1, 64);
    size_spin_->set_increments(1, 4);
    size_spin_->set_digits(0);
    size_spin_->set_value(host_ != nullptr ? host_->stroke_size() : 1);
    size_spin_->signal_value_changed().connect([this]() {
      if (host_ != nullptr) {
        host_->set_stroke_size(size_spin_->get_value_as_int());
      }
    });
    options_->pack_start(*label, Gtk::PACK_SHRINK);
    options_->pack_start(*size_spin_, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
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
  stamp_to(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)));
}

void PencilTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)));
  finish_stroke();
}

void PencilTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(dirty_);
  dirty_ = {};
}

void PencilTool::begin_stroke(CanvasEvent event) {
  if (host_ == nullptr) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  host_->document().commit_floating();
  drawing_ = true;
  button_ = event.button;
  last_x_ = static_cast<int>(std::floor(event.x));
  last_y_ = static_cast<int>(std::floor(event.y));
  dirty_ = {};
  host_->document().layers().copy_active_to_tool();
  stamp_to(last_x_, last_y_);
}

void PencilTool::stamp_to(int x, int y) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Layer& tool = host_->document().layers().tool_layer();
  const Color color = stroke_color(button_);
  const int size = host_->stroke_size();
  stroke_pencil(tool.pixels(), tool.width(), tool.height(), tool.stride(), last_x_, last_y_, x, y,
                size, color, &dirty_);
  clip_rect_to_selection(tool, host_->document().layers().active_layer(), dirty_,
                         host_->document().selection());
  last_x_ = x;
  last_y_ = y;
  host_->invalidate_canvas(dirty_);
}

void PencilTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Pencil stroke", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
  dirty_ = {};
}

Tool* create_pencil_tool() {
  return new PencilTool();
}

}  // namespace lundukepaint
