// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/fill.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>

namespace lundukepaint {

class FillTool : public Tool {
public:
  const char* id() const override { return "fill"; }
  const char* name() const override { return "Flood fill"; }
  char shortcut() const override { return 'F'; }
  const char* hint() const override { return "Fill: click a region; right uses BG"; }
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
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* label = Gtk::manage(new Gtk::Label("Similarity"));
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(0, 255);
    spin->set_increments(1, 16);
    spin->set_digits(0);
    spin->set_value(tolerance_);
    spin->set_tooltip_text("0 = exact color, 255 = fill every connected pixel");
    spin->signal_value_changed().connect([this, spin]() {
      tolerance_ = spin->get_value_as_int();
      if (host_ != nullptr) {
        host_->set_fill_tolerance(tolerance_);
      }
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
  doc.commit_floating();
  doc.layers().copy_active_to_tool();
  Layer& tool = doc.layers().tool_layer();
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  Rect dirty{};
  flood_fill(tool.pixels(), tool.width(), tool.height(), tool.stride(), x, y,
             stroke_color(event.button), tolerance_, &dirty);
  clip_rect_to_selection(tool, doc.layers().active_layer(), dirty, doc.selection());
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
