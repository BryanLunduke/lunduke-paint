// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/stroke.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cstdint>
#include <memory>

namespace lundukepaint {

class SprayTool : public Tool {
public:
  const char* id() const override { return "spray"; }
  const char* name() const override { return "Spraycan"; }
  char shortcut() const override { return 'Y'; }
  const char* hint() const override { return "Spraycan: drag; right uses BG"; }
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
  unsigned button_ = 1;
  int radius_ = 16;
  int density_ = 40;
  std::uint32_t rng_ = 0xA5A5A5A5u;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* SprayTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* rlabel = Gtk::manage(new Gtk::Label("Radius"));
    auto* rspin = Gtk::manage(new Gtk::SpinButton());
    rspin->set_range(1, 64);
    rspin->set_increments(1, 4);
    rspin->set_digits(0);
    rspin->set_value(radius_);
    rspin->signal_value_changed().connect([this, rspin]() { radius_ = rspin->get_value_as_int(); });
    auto* dlabel = Gtk::manage(new Gtk::Label("Density"));
    auto* dspin = Gtk::manage(new Gtk::SpinButton());
    dspin->set_range(1, 100);
    dspin->set_increments(1, 10);
    dspin->set_digits(0);
    dspin->set_value(density_);
    dspin->set_tooltip_text("Dots per stamp (1 = sparse, 100 = heavy)");
    dspin->signal_value_changed().connect([this, dspin]() { density_ = dspin->get_value_as_int(); });
    options_->pack_start(*rlabel, Gtk::PACK_SHRINK);
    options_->pack_start(*rspin, Gtk::PACK_SHRINK);
    options_->pack_start(*dlabel, Gtk::PACK_SHRINK);
    options_->pack_start(*dspin, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
}

void SprayTool::on_press(CanvasEvent event) {
  if (event.button != 1 && event.button != 3) {
    return;
  }
  begin_stroke(event);
}

void SprayTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    stamp_to(event.x, event.y);
  }
}

void SprayTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  stamp_to(event.x, event.y);
  finish_stroke();
}

void SprayTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(dirty_);
  dirty_ = {};
}

void SprayTool::begin_stroke(CanvasEvent event) {
  if (host_ == nullptr || !ensure_editable()) {
    return;
  }
  host_->document().commit_floating();
  drawing_ = true;
  button_ = event.button;
  dirty_ = {};
  host_->document().layers().copy_active_to_tool();
  stamp_to(event.x, event.y);
}

void SprayTool::stamp_to(double x, double y) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Document& doc = host_->document();
  Layer& tool = doc.layers().tool_layer();
  spray_dots(tool.pixels(), tool.width(), tool.height(), tool.stride(), x, y, radius_, density_,
             stroke_color(button_), &rng_, &dirty_);
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty_, doc.selection());
  host_->invalidate_canvas(dirty_);
}

void SprayTool::finish_stroke() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Spray", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
  dirty_ = {};
}

Tool* create_spray_tool() {
  return new SprayTool();
}

}  // namespace lundukepaint
