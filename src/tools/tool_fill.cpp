// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/rail_options.hpp"
#include "app/live_edit.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/fill.hpp"
#include "raster/gradient.hpp"
#include "raster/pattern.hpp"

#include <gtkmm/box.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace lundukepaint {

class FillTool : public Tool {
public:
  const char* id() const override { return "fill"; }
  const char* name() const override { return "Flood fill"; }
  char shortcut() const override { return 'F'; }
  const char* hint() const override {
    if (mode_ == 1) {
      return "Linear gradient: drag; dithered from FG to BG";
    }
    if (mode_ == 2) {
      return "Radial gradient: drag from the center; dithered from FG to BG";
    }
    return "Fill: click a region; uses the pattern strip; right uses BG";
  }
  Gtk::Widget* options_widget() override;
  bool is_stroking() const override { return dragging_; }

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  bool build_mask(int x, int y);
  void paint_gradient(int x1, int y1);
  void restore_base();
  bool finish_gradient();

  int tolerance_ = 0;
  int mode_ = 0;
  bool dragging_ = false;
  int x0_ = 0;
  int y0_ = 0;
  unsigned button_ = 1;
  Rect mask_bounds_{};
  std::vector<std::uint8_t> mask_;
  std::vector<std::uint8_t> base_;
  Rect dirty_{};
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
    auto* mode_label = Gtk::manage(new Gtk::Label("Fill"));
    mode_label->set_halign(Gtk::ALIGN_START);
    auto* combo = Gtk::manage(new Gtk::ComboBoxText());
    combo->append("Solid");
    combo->append("Linear");
    combo->append("Radial");
    combo->set_active(mode_);
    combo->set_tooltip_text("Solid flood, or a two-color Bayer gradient");
    configure_rail_combo(*combo);
    combo->signal_changed().connect([this, combo]() { mode_ = combo->get_active_row_number(); });
    options_->pack_start(*label, Gtk::PACK_SHRINK);
    options_->pack_start(*spin, Gtk::PACK_SHRINK);
    options_->pack_start(*mode_label, Gtk::PACK_SHRINK);
    options_->pack_start(*combo, Gtk::PACK_SHRINK);
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
  if (mode_ == 1 || mode_ == 2) {
    doc.layers().copy_active_to_tool();
    const Layer& active = doc.layers().active_layer();
    x0_ = static_cast<int>(std::floor(event.x)) - active.offset_x();
    y0_ = static_cast<int>(std::floor(event.y)) - active.offset_y();
    button_ = event.button;
    if (!build_mask(x0_, y0_)) {
      doc.layers().clear_tool_layer();
      return;
    }
    dragging_ = true;
    dirty_ = {};
    paint_gradient(x0_, y0_);
    return;
  }
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

void FillTool::on_motion(CanvasEvent event) {
  if (!dragging_ || host_ == nullptr) {
    return;
  }
  const Layer& active = host_->document().layers().active_layer();
  const int x = static_cast<int>(std::floor(event.x)) - active.offset_x();
  const int y = static_cast<int>(std::floor(event.y)) - active.offset_y();
  paint_gradient(x, y);
}

void FillTool::on_release(CanvasEvent event) {
  if (!dragging_) {
    return;
  }
  const Layer& active = host_->document().layers().active_layer();
  const int x = static_cast<int>(std::floor(event.x)) - active.offset_x();
  const int y = static_cast<int>(std::floor(event.y)) - active.offset_y();
  paint_gradient(x, y);
  finish_gradient();
}

void FillTool::on_cancel() {
  if (!dragging_ || host_ == nullptr) {
    dragging_ = false;
    return;
  }
  dragging_ = false;
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(layer_dirty_to_canvas(preview_layer_ref(), dirty_));
  dirty_ = {};
  mask_.clear();
  base_.clear();
}

bool FillTool::build_mask(int x, int y) {
  mask_.clear();
  base_.clear();
  mask_bounds_ = {};
  Document& doc = host_->document();
  const Layer& active = doc.layers().active_layer();
  const Selection& sel = doc.selection();
  if (!sel.empty() && !sel.inverted()) {
    const Rect canvas = sel.bounds();
    Rect local{canvas.x - active.offset_x(), canvas.y - active.offset_y(), canvas.w, canvas.h};
    local = rect_intersect(local, Rect{0, 0, active.width(), active.height()});
    if (local.empty()) {
      return false;
    }
    mask_.assign(static_cast<std::size_t>(local.w) * static_cast<std::size_t>(local.h), 0);
    bool any = false;
    for (int row = 0; row < local.h; ++row) {
      for (int col = 0; col < local.w; ++col) {
        const int cx = local.x + col + active.offset_x();
        const int cy = local.y + row + active.offset_y();
        if (sel.contains(cx, cy)) {
          mask_[static_cast<std::size_t>(row) * static_cast<std::size_t>(local.w) +
                static_cast<std::size_t>(col)] = 255;
          any = true;
        }
      }
    }
    if (!any) {
      return false;
    }
    mask_bounds_ = local;
  } else {
    Layer& tool = doc.layers().tool_layer();
    flood_mask(tool.pixels(), tool.width(), tool.height(), tool.stride(), x, y, tolerance_, mask_,
               &mask_bounds_);
    if (mask_bounds_.empty() || mask_.empty()) {
      return false;
    }
  }
  base_.assign(static_cast<std::size_t>(mask_bounds_.w) * static_cast<std::size_t>(mask_bounds_.h) * 4,
               0);
  const Layer& tool = doc.layers().tool_layer();
  for (int row = 0; row < mask_bounds_.h; ++row) {
    std::memcpy(base_.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(mask_bounds_.w) * 4,
                tool.pixels() + static_cast<std::size_t>(mask_bounds_.y + row) * tool.stride() +
                    static_cast<std::size_t>(mask_bounds_.x) * 4,
                static_cast<std::size_t>(mask_bounds_.w) * 4);
  }
  return true;
}

void FillTool::restore_base() {
  if (base_.empty() || host_ == nullptr) {
    return;
  }
  Layer& tool = host_->document().layers().tool_layer();
  for (int row = 0; row < mask_bounds_.h; ++row) {
    std::memcpy(tool.pixels() + static_cast<std::size_t>(mask_bounds_.y + row) * tool.stride() +
                    static_cast<std::size_t>(mask_bounds_.x) * 4,
                base_.data() + static_cast<std::size_t>(row) * static_cast<std::size_t>(mask_bounds_.w) * 4,
                static_cast<std::size_t>(mask_bounds_.w) * 4);
  }
}

void FillTool::paint_gradient(int x1, int y1) {
  if (host_ == nullptr || mask_bounds_.empty()) {
    return;
  }
  restore_base();
  Document& doc = host_->document();
  const Layer& active = doc.layers().active_layer();
  Layer& tool = doc.layers().tool_layer();
  const Color c0 = stroke_color(button_);
  const Color c1 = (button_ == 3) ? doc.foreground() : doc.background();
  Rect dirty{};
  gradient_dither(tool.pixels(), tool.width(), tool.height(), tool.stride(), x0_, y0_, x1, y1, c0, c1,
                  mode_ == 2 ? GradientKind::Radial : GradientKind::Linear, mask_.data(),
                  mask_bounds_.x, mask_bounds_.y, mask_bounds_.w, mask_bounds_.h, &dirty);
  clip_rect_to_selection(tool, active, dirty, doc.selection());
  dirty_ = rect_union(dirty_, dirty);
  host_->invalidate_canvas(layer_dirty_to_canvas(active, dirty_));
}

bool FillTool::finish_gradient() {
  dragging_ = false;
  if (host_ == nullptr) {
    return false;
  }
  const bool ok = commit_preview("Gradient", dirty_);
  dirty_ = {};
  mask_.clear();
  base_.clear();
  return ok;
}

Tool* create_fill_tool() {
  return new FillTool();
}

}  // namespace lundukepaint
