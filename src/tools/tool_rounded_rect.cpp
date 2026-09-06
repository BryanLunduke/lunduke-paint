// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

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

namespace lundukepaint {

class RoundedRectTool : public Tool {
public:
  const char* id() const override { return "rounded-rect"; }
  const char* name() const override { return "Rounded rectangle"; }
  char shortcut() const override { return 'U'; }
  const char* hint() const override {
    return "Rounded rect: drag; Shift makes a square; right uses BG";
  }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  void preview(int x1, int y1, bool constrain);
  void finish();

  bool drawing_ = false;
  int x0_ = 0;
  int y0_ = 0;
  int x1_ = 0;
  int y1_ = 0;
  unsigned button_ = 1;
  int thickness_ = 1;
  int radius_ = 12;
  bool antialias_ = false;
  ShapeFillMode fill_mode_ = ShapeFillMode::Stroke;
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* RoundedRectTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* mlabel = Gtk::manage(new Gtk::Label("Mode"));
    auto* combo = Gtk::manage(new Gtk::ComboBoxText());
    combo->append("stroke", "Stroke");
    combo->append("fill", "Fill");
    combo->append("both", "Stroke and fill");
    combo->set_active(0);
    combo->signal_changed().connect([this, combo]() {
      const Glib::ustring id = combo->get_active_id();
      if (id == "fill") {
        fill_mode_ = ShapeFillMode::Fill;
      } else if (id == "both") {
        fill_mode_ = ShapeFillMode::Both;
      } else {
        fill_mode_ = ShapeFillMode::Stroke;
      }
    });
    auto* tlabel = Gtk::manage(new Gtk::Label("Thickness"));
    auto* tspin = Gtk::manage(new Gtk::SpinButton());
    tspin->set_range(1, 64);
    tspin->set_increments(1, 4);
    tspin->set_digits(0);
    tspin->set_value(thickness_);
    tspin->signal_value_changed().connect([this, tspin]() { thickness_ = tspin->get_value_as_int(); });
    auto* rlabel = Gtk::manage(new Gtk::Label("Corner"));
    auto* rspin = Gtk::manage(new Gtk::SpinButton());
    rspin->set_range(0, 256);
    rspin->set_increments(1, 8);
    rspin->set_digits(0);
    rspin->set_value(radius_);
    rspin->set_tooltip_text("Corner radius in pixels");
    rspin->signal_value_changed().connect([this, rspin]() { radius_ = rspin->get_value_as_int(); });
    auto* aa = Gtk::manage(new Gtk::CheckButton("Anti-alias"));
    aa->set_active(antialias_);
    aa->signal_toggled().connect([this, aa]() { antialias_ = aa->get_active(); });
    options_->pack_start(*mlabel, Gtk::PACK_SHRINK);
    options_->pack_start(*combo, Gtk::PACK_SHRINK);
    options_->pack_start(*tlabel, Gtk::PACK_SHRINK);
    options_->pack_start(*tspin, Gtk::PACK_SHRINK);
    options_->pack_start(*rlabel, Gtk::PACK_SHRINK);
    options_->pack_start(*rspin, Gtk::PACK_SHRINK);
    options_->pack_start(*aa, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
}

void RoundedRectTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  host_->document().commit_floating();
  drawing_ = true;
  button_ = event.button;
  x0_ = static_cast<int>(std::floor(event.x));
  y0_ = static_cast<int>(std::floor(event.y));
  x1_ = x0_;
  y1_ = y0_;
  dirty_ = {};
  host_->document().layers().copy_active_to_tool();
  preview(x1_, y1_, false);
}

void RoundedRectTool::preview(int x1, int y1, bool constrain) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Document& doc = host_->document();
  Layer& tool = doc.layers().tool_layer();
  tool.copy_from(doc.layers().active_layer());
  x1_ = x1;
  y1_ = y1;
  if (constrain) {
    constrain_square(x0_, y0_, &x1_, &y1_);
  }
  dirty_ = {};
  draw_rounded_rect(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0_, y0_, x1_, y1_,
                    thickness_, radius_, stroke_color(button_), fill_mode_, antialias_, &dirty_);
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty_, doc.selection());
  host_->invalidate_canvas(dirty_);
}

void RoundedRectTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    preview(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
            (event.modifiers & Modifier::Shift) != 0);
  }
}

void RoundedRectTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  preview(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
          (event.modifiers & Modifier::Shift) != 0);
  finish();
}

void RoundedRectTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(dirty_);
  dirty_ = {};
}

void RoundedRectTool::finish() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Rounded rectangle", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
  dirty_ = {};
}

Tool* create_rounded_rect_tool() {
  return new RoundedRectTool();
}

}  // namespace lundukepaint
