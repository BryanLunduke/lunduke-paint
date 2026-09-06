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

class LineTool : public Tool {
public:
  const char* id() const override { return "line"; }
  const char* name() const override { return "Line"; }
  char shortcut() const override { return 'L'; }
  const char* hint() const override { return "Line: drag; Shift constrains to 45°; right uses BG"; }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;

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
  std::unique_ptr<Gtk::Box> options_;
  Gtk::ComboBoxText* mode_combo_{nullptr};
};

Gtk::Widget* LineTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    if (false) {
      auto* label = Gtk::manage(new Gtk::Label("Mode"));
      mode_combo_ = Gtk::manage(new Gtk::ComboBoxText());
      mode_combo_->append("stroke", "Stroke");
      mode_combo_->append("fill", "Fill");
      mode_combo_->append("both", "Stroke and fill");
      mode_combo_->set_active(0);
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

ShapeFillMode LineTool::mode() const {
  return fill_mode_;
}

void LineTool::on_press(CanvasEvent event) {
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

void LineTool::preview(int x1, int y1, bool constrain) {
  if (host_ == nullptr || !drawing_) {
    return;
  }
  Document& doc = host_->document();
  Layer& tool = doc.layers().tool_layer();
  tool.copy_from(doc.layers().active_layer());
  x1_ = x1;
  y1_ = y1;
  if (constrain) {
    constrain_line_45(x0_, y0_, &x1_, &y1_);
  }
  dirty_ = {};
  draw_line(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0_, y0_, x1_, y1_, thickness_, stroke_color(button_), antialias_, &dirty_);
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty_, doc.selection());
  host_->invalidate_canvas(dirty_);
}

void LineTool::on_motion(CanvasEvent event) {
  if (drawing_) {
    preview(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
            (event.modifiers & Modifier::Shift) != 0);
  }
}

void LineTool::on_release(CanvasEvent event) {
  if (!drawing_) {
    return;
  }
  preview(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
          (event.modifiers & Modifier::Shift) != 0);
  finish();
}

void LineTool::on_cancel() {
  if (!drawing_ || host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(dirty_);
  dirty_ = {};
}

void LineTool::finish() {
  if (host_ == nullptr) {
    drawing_ = false;
    return;
  }
  drawing_ = false;
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Line", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(dirty_);
  }
  dirty_ = {};
}

Tool* create_line_tool() {
  return new LineTool();
}

}  // namespace lundukepaint
