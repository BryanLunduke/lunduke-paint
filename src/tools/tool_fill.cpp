// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/rail_options.hpp"
#include "app/live_edit.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/fill.hpp"
#include "raster/pattern.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>
#include <vector>

namespace lundukepaint {

class FillTool : public Tool {
public:
  const char* id() const override { return "fill"; }
  const char* name() const override { return "Flood fill"; }
  char shortcut() const override { return 'F'; }
  const char* hint() const override {
    return "Fill: click a region; uses the pattern strip; right uses BG";
  }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent /*event*/) override {}
  void on_release(CanvasEvent /*event*/) override {}
  void on_cancel() override {}

private:
  int tolerance_ = 0;
  std::unique_ptr<Gtk::Box> options_;
};

Gtk::Widget* FillTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
    prepare_rail_box(*options_);
    auto* label = Gtk::manage(new Gtk::Label("Similarity"));
    label->set_halign(Gtk::ALIGN_START);
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(0, 255);
    spin->set_increments(1, 16);
    spin->set_digits(0);
    spin->set_value(tolerance_);
    spin->set_tooltip_text("0 = exact color, 255 = fill every connected pixel");
    configure_rail_spin(*spin);
    spin->signal_value_changed().connect([this, spin]() {
      tolerance_ = spin->get_value_as_int();
    });
    options_->pack_start(*label, Gtk::PACK_SHRINK);
    options_->pack_start(*spin, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
}

void FillTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  Document& doc = host_->document();
  const Layer& gate = doc.layers().active_layer();
  if ((gate.width() > kSoftMaxSide || gate.height() > kSoftMaxSide) &&
      !host_->confirm_large_canvas(gate.width(), gate.height())) {
    return;
  }
  if (!commit_float_or_stop()) {
    return;
  }
  tolerance_ = tolerance_for_click(tolerance_, host_->fill_tolerance());
  doc.layers().copy_active_to_tool();
  const Layer& active = doc.layers().active_layer();
  Layer& tool = doc.layers().tool_layer();
  const int x = static_cast<int>(std::floor(event.x)) - active.offset_x();
  const int y = static_cast<int>(std::floor(event.y)) - active.offset_y();
  Rect dirty{};
  const Color paint = stroke_color(event.button);
  const Color other = (event.button == 3) ? doc.foreground() : doc.background();
  // Solid black pattern (index 0): classic solid flood. Else tile the pattern.
  if (host_->pattern_index() == 0) {
    flood_fill(tool.pixels(), tool.width(), tool.height(), tool.stride(), x, y, paint, tolerance_,
               &dirty);
  } else {
    std::vector<std::uint8_t> mask;
    Rect bounds{};
    flood_mask(tool.pixels(), tool.width(), tool.height(), tool.stride(), x, y, tolerance_, mask,
               &bounds);
    if (!bounds.empty()) {
      apply_pattern_mask(tool.pixels(), tool.width(), tool.height(), tool.stride(), mask.data(),
                         bounds.x, bounds.y, bounds.w, bounds.h, host_->active_pattern(), paint,
                         other, &dirty);
    }
  }
  clip_rect_to_selection(tool, active, dirty, doc.selection());
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), tool, dirty, "Flood fill",
                                            doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  }
}

Tool* create_fill_tool() {
  return new FillTool();
}

}  // namespace lundukepaint
