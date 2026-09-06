// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "tools/tool_options_ui.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/stroke.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>

namespace lundukepaint {

class ColorEraserTool : public Tool {
public:
  const char* id() const override { return "color-eraser"; }
  const char* name() const override { return "Color eraser"; }
  char shortcut() const override { return 'O'; }
  const char* hint() const override {
    return "Color eraser: replace FG-similar pixels with BG; right swaps roles";
  }
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

  bool drawing_ = false;
  double last_x_ = 0;
  double last_y_ = 0;
  unsigned button_ = 1;
  int size_ = 12;
  int tolerance_ = 16;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* ColorEraserTool::options_widget() {
  if (!options_) {
    options_ = tool_options_ui::make_column();
    auto* sspin = tool_options_ui::make_spin(1, 64, 1, size_);
    sspin->signal_value_changed().connect([this, sspin]() { size_ = sspin->get_value_as_int(); });
    tool_options_ui::pack_labeled(*options_, "Size", "Color eraser size", *sspin);
    auto* tspin = tool_options_ui::make_spin(0, 255, 1, tolerance_, 16);
    tspin->set_tooltip_text("0 = exact FG color, 255 = erase everything in the stroke");
    tspin->signal_value_changed().connect([this, tspin]() { tolerance_ = tspin->get_value_as_int(); });
    tool_options_ui::pack_labeled(*options_, "Sim", "Color similarity", *tspin);
    options_->show_all();
  }
  return options_.get();
}

void ColorEraserTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  begin_stroke(event);
}

void ColorEraserTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    stamp_to(event.x, event.y);
  }
}

void ColorEraserTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(event.x, event.y);
  finish_stroke();
}

void ColorEraserTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(dirty_);
  dirty_ = {};
}

void ColorEraserTool::begin_stroke(CanvasEvent event) {
  if (host_ == nullptr || !ensure_editable()) {
    return;
  }
  host_->document().commit_floating();
  drawing_ = true;
  button_ = event.button;
  last_x_ = event.x;
  last_y_ = event.y;
  dirty_ = {};
  host_->document().layers().copy_active_to_tool();
  stamp_to(last_x_, last_y_);
}

void ColorEraserTool::stamp_to(double x, double y) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Document& doc = host_->document();
  Layer& tool = doc.layers().tool_layer();
  const Color target = (button_ == 3) ? doc.background() : doc.foreground();
  const Color replacement = (button_ == 3) ? doc.foreground() : doc.background();
  color_erase_stroke(tool.pixels(), tool.width(), tool.height(), tool.stride(), last_x_, last_y_, x,
                     y, size_, target, replacement, tolerance_, &dirty_);
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty_, doc.selection());
  last_x_ = x;
  last_y_ = y;
  host_->invalidate_canvas(dirty_);
}

void ColorEraserTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Color eraser", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
  dirty_ = {};
}

Tool* create_color_eraser_tool() {
  return new ColorEraserTool();
}

}  // namespace lundukepaint
