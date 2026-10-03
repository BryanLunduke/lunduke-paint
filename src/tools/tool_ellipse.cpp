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

class EllipseTool : public Tool {
public:
  explicit EllipseTool(ShapeFillMode mode = ShapeFillMode::Stroke, const char* tool_id = "ellipse",
                       const char* tool_name = "Ellipse")
      : fill_mode_(mode), id_(tool_id), name_(tool_name) {}
  const char* id() const override { return id_; }
  const char* name() const override { return name_; }
  char shortcut() const override { return 'E'; }
  const char* hint() const override { return "Ellipse: drag; Shift makes a circle; right uses BG"; }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;
  void set_shape_fill_mode(ShapeFillMode mode) override {
    fill_mode_ = mode;
    if (mode_combo_ != nullptr) {
      if (mode == ShapeFillMode::Fill) mode_combo_->set_active_id("fill");
      else if (mode == ShapeFillMode::Both) mode_combo_->set_active_id("both");
      else mode_combo_->set_active_id("stroke");
    }
  }

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  void preview(int x1, int y1, bool constrain);
  void finish();
  ShapeFillMode mode() const;

  bool drawing_ = false;
  int x0_ = 0;
  int y0_ = 0;
  int x1_ = 0;
  int y1_ = 0;
  unsigned button_ = 1;
  int thickness_ = 1;
  bool antialias_ = false;
  ShapeFillMode fill_mode_ = ShapeFillMode::Stroke;
  Rect dirty_{};
  const char* id_ = "ellipse";
  const char* name_ = "Ellipse";
  std::unique_ptr<Gtk::Box> options_;
  Gtk::ComboBoxText* mode_combo_{nullptr};
};

Gtk::Widget* EllipseTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    if (true) {
      auto* label = Gtk::manage(new Gtk::Label("Mode"));
      mode_combo_ = Gtk::manage(new Gtk::ComboBoxText());
      mode_combo_->append("stroke", "Stroke");
      mode_combo_->append("fill", "Fill");
      mode_combo_->append("both", "Stroke and fill");
      if (fill_mode_ == ShapeFillMode::Fill) mode_combo_->set_active_id("fill");
      else if (fill_mode_ == ShapeFillMode::Both) mode_combo_->set_active_id("both");
      else mode_combo_->set_active_id("stroke");
      mode_combo_->signal_changed().connect([this]() {
        const Glib::ustring id = mode_combo_->get_active_id();
        if (id == "fill") {
          fill_mode_ = ShapeFillMode::Fill;
        } else if (id == "both") {
          fill_mode_ = ShapeFillMode::Both;
        } else {
          fill_mode_ = ShapeFillMode::Stroke;
        }
      });
      options_->pack_start(*label, Gtk::PACK_SHRINK);
      options_->pack_start(*mode_combo_, Gtk::PACK_SHRINK);
    }
    auto* tlabel = Gtk::manage(new Gtk::Label("Thickness"));
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(1, 64);
    spin->set_increments(1, 4);
    spin->set_digits(0);
    spin->set_value(thickness_);
    spin->signal_value_changed().connect([this, spin]() {
      thickness_ = spin->get_value_as_int();
    });
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

ShapeFillMode EllipseTool::mode() const {
  return fill_mode_;
}

void EllipseTool::on_press(CanvasEvent event) {
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

void EllipseTool::preview(int x1, int y1, bool constrain) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Document& doc = host_->document();
  const Layer& active = doc.layers().active_layer();
  Layer& tool = doc.layers().tool_layer();
  tool.copy_from(active);
  x1_ = x1;
  y1_ = y1;
  if (constrain) {
    constrain_square(x0_, y0_, &x1_, &y1_);
  }
  dirty_ = {};
  const int ox = active.offset_x();
  const int oy = active.offset_y();
  draw_ellipse(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0_ - ox, y0_ - oy, x1_ - ox,
               y1_ - oy, thickness_, stroke_color(button_), mode(), antialias_, &dirty_);
  clip_rect_to_selection(tool, active, dirty_, doc.selection());
  host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
}

void EllipseTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    preview(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
            (event.modifiers & Modifier::Shift) != 0);
  }
}

void EllipseTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  preview(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
          (event.modifiers & Modifier::Shift) != 0);
  finish();
}

void EllipseTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  dirty_ = {};
}

void EllipseTool::finish() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Ellipse", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  }
  dirty_ = {};
}

Tool* create_ellipse_tool() {
  return new EllipseTool(ShapeFillMode::Stroke, "ellipse", "Ellipse");
}

Tool* create_ellipse_fill_tool() {
  return new EllipseTool(ShapeFillMode::Fill, "ellipse-fill", "Ellipse fill");
}

}  // namespace lundukepaint
