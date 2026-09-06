// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/selection_xform.hpp"

#include "doc/document.hpp"
#include "raster/fill.hpp"

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <memory>
#include <vector>

namespace lundukepaint {

class MagicWandTool : public Tool {
public:
  const char* id() const override { return "magic-wand"; }
  const char* name() const override { return "Magic wand"; }
  char shortcut() const override { return 'W'; }
  const char* hint() const override {
    return "Magic wand: click a color; drag a handle to scale/rotate";
  }
  bool is_stroking() const override { return xform_.active(); }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  int tolerance_ = 0;
  std::unique_ptr<Gtk::Box> options_;
  SelectionXform xform_;
};

Gtk::Widget* MagicWandTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* label = Gtk::manage(new Gtk::Label("Similarity"));
    auto* spin = Gtk::manage(new Gtk::SpinButton());
    spin->set_range(0, 255);
    spin->set_increments(1, 16);
    spin->set_digits(0);
    spin->set_value(tolerance_);
    spin->set_tooltip_text("0 = exact color, 255 = select every connected pixel");
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

void MagicWandTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (xform_.on_press(host_, event, host_->canvas_zoom())) {
    return;
  }
  Document& doc = host_->document();
  doc.commit_floating();
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  if (x < 0 || y < 0 || x >= doc.width() || y >= doc.height()) {
    return;
  }
  const Layer& layer = doc.layers().active_layer();
  std::vector<std::uint8_t> full;
  Rect bounds{};
  flood_mask(layer.pixels(), layer.width(), layer.height(), layer.stride(), x, y, tolerance_, full,
             &bounds);
  if (bounds.empty()) {
    const Rect dirty = doc.selection().bounds();
    doc.selection().clear();
    host_->invalidate_canvas(dirty);
    doc.notify_changed();
    return;
  }
  std::vector<std::uint8_t> tight(static_cast<std::size_t>(bounds.w) *
                                      static_cast<std::size_t>(bounds.h),
                                  0);
  for (int yy = 0; yy < bounds.h; ++yy) {
    for (int xx = 0; xx < bounds.w; ++xx) {
      const int src = (bounds.y + yy) * layer.width() + (bounds.x + xx);
      tight[static_cast<std::size_t>(yy) * bounds.w + static_cast<std::size_t>(xx)] = full[src];
    }
  }
  const Rect old = doc.selection().dirty_union();
  doc.selection().set_mask(bounds, std::move(tight));
  host_->invalidate_canvas(rect_union(old, doc.selection().dirty_union()));
  doc.notify_changed();
}

void MagicWandTool::on_motion(CanvasEvent event) {
  if (xform_.active()) {
    xform_.on_motion(host_, event);
  }
}

void MagicWandTool::on_release(CanvasEvent /*event*/) {
  if (xform_.active()) {
    xform_.on_release(host_);
  }
}

void MagicWandTool::on_cancel() {
  xform_.on_cancel(host_);
}

Tool* create_magic_wand_tool() {
  return new MagicWandTool();
}

}  // namespace lundukepaint
