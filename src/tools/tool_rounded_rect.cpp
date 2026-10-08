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
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>

namespace lundukepaint {

class RoundedRectTool : public Tool {
public:
  explicit RoundedRectTool(ShapeFillMode mode = ShapeFillMode::Stroke,
                           const char* tool_id = "rounded-rect",
                           const char* tool_name = "Rounded rectangle")
      : fill_mode_(mode), id_(tool_id), name_(tool_name) {}
  const char* id() const override { return id_; }
  const char* name() const override { return name_; }
  char shortcut() const override { return 'U'; }
  const char* hint() const override {
    return "Rounded rect: drag; Shift makes a square; right uses BG";
  }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;
  void set_shape_fill_mode(ShapeFillMode mode) override { fill_mode_ = mode; }

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;
  bool on_commit() override;

private:
  void preview(int x1, int y1, bool constrain);
  bool finish();

  bool drawing_ = false;
  int x0_ = 0;
  int y0_ = 0;
  int x1_ = 0;
  int y1_ = 0;
  unsigned button_ = 1;
  int radius_ = 12;
  bool antialias_ = false;
  const char* family_ = "rounded-rect";
  Gtk::CheckButton* aa_button_ = nullptr;
  Gtk::SpinButton* radius_spin_ = nullptr;
  ShapeFillMode fill_mode_ = ShapeFillMode::Stroke;
  const char* id_ = "rounded-rect";
  const char* name_ = "Rounded rectangle";
  Rect dirty_{};
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* RoundedRectTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    prepare_rail_box(*options_);
    auto* rlabel = Gtk::manage(new Gtk::Label("Corner"));
    rlabel->set_halign(Gtk::ALIGN_START);
    auto* rspin = Gtk::manage(new Gtk::SpinButton());
    rspin->set_range(0, 256);
    rspin->set_increments(1, 8);
    rspin->set_digits(0);
    radius_spin_ = rspin;
    rspin->set_tooltip_text("Corner radius in pixels");
    configure_rail_spin(*rspin);
    rspin->signal_value_changed().connect([this, rspin]() {
      radius_ = rspin->get_value_as_int();
      shape_family_options(family_).corner_radius = radius_;
    });
    auto* aa = Gtk::manage(new Gtk::CheckButton("Anti-alias"));
    aa_button_ = aa;
    aa->set_tooltip_text("Smooth the rounded corners");
    configure_rail_check(*aa);
    aa->signal_toggled().connect([this, aa]() {
      antialias_ = aa->get_active();
      shape_family_options(family_).antialias = antialias_;
    });
    options_->pack_start(*rlabel, Gtk::PACK_SHRINK);
    options_->pack_start(*rspin, Gtk::PACK_SHRINK);
    options_->pack_start(*aa, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  const ShapeFamilyOptions& shared = shape_family_options(family_);
  antialias_ = shared.antialias;
  radius_ = shared.corner_radius;
  if (aa_button_ != nullptr && aa_button_->get_active() != antialias_) {
    aa_button_->set_active(antialias_);
  }
  if (radius_spin_ != nullptr && radius_spin_->get_value_as_int() != radius_) {
    radius_spin_->set_value(radius_);
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
  if (!commit_float_or_stop()) {
    return;
  }
  drawing_ = true;
  button_ = event.button;
  x0_ = static_cast<int>(std::floor(event.x));
  y0_ = static_cast<int>(std::floor(event.y));
  x1_ = x0_;
  y1_ = y0_;
  dirty_ = {};
  arm_preview_layer();
  preview(x1_, y1_, false);
}

void RoundedRectTool::preview(int x1, int y1, bool constrain) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Document& doc = host_->document();
  antialias_ = shape_family_options(family_).antialias;
  radius_ = shape_family_options(family_).corner_radius;
  const Layer& active = preview_layer_ref();
  Layer& tool = doc.layers().tool_layer();
  const Rect previous = dirty_;
  restore_shape_preview(tool, active, previous);
  x1_ = x1;
  y1_ = y1;
  if (constrain) {
    constrain_square(x0_, y0_, &x1_, &y1_);
  }
  dirty_ = {};
  const int ox = active.offset_x();
  const int oy = active.offset_y();
  draw_rounded_rect(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0_ - ox, y0_ - oy,
                    x1_ - ox, y1_ - oy, stroke_px(), radius_, stroke_color(button_), fill_mode_,
                    antialias_, &dirty_, shape_pattern(fill_mode_), pattern_back(button_));
  clip_rect_to_selection(tool, active, dirty_, doc.selection());
  host_->invalidate_canvas(layer_dirty_to_canvas(active, rect_union(previous, dirty_)));
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
    clear_preview_overlay();
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(layer_dirty_to_canvas(preview_layer_ref(), dirty_));
  dirty_ = {};
  clear_preview_overlay();
}

bool RoundedRectTool::on_commit() {
  if (!drawing_) {
    return false;
  }
  return finish();
}

bool RoundedRectTool::finish() {
  if (host_ == nullptr) {
    drawing_ = false;
    return false;
  }
  radius_ = shape_family_options(family_).corner_radius;
  antialias_ = shape_family_options(family_).antialias;
  if (!commit_preview("Rounded rectangle", dirty_)) {
    return false;
  }
  drawing_ = false;
  dirty_ = {};
  return true;
}

Tool* create_rounded_rect_tool() {
  return new RoundedRectTool(ShapeFillMode::Stroke, "rounded-rect", "Rounded rectangle");
}

Tool* create_rounded_rect_fill_tool() {
  return new RoundedRectTool(ShapeFillMode::Fill, "rounded-rect-fill", "Rounded rectangle fill");
}

}  // namespace lundukepaint
