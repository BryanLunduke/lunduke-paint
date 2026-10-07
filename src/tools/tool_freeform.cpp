// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/shapes.hpp"

#include <gtkmm/box.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>

#include <cmath>
#include <memory>
#include <vector>

namespace lundukepaint {

class FreeformShapeTool : public Tool {
public:
  explicit FreeformShapeTool(ShapeFillMode mode, const char* tool_id, const char* tool_name)
      : fill_mode_(mode), id_(tool_id), name_(tool_name) {}

  const char* id() const override { return id_; }
  const char* name() const override { return name_; }
  char shortcut() const override { return fill_mode_ == ShapeFillMode::Fill ? 'O' : 'D'; }
  const char* hint() const override {
    return "Freeform shape: drag a freehand outline; right uses BG";
  }
  bool is_stroking() const override { return drawing_; }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;
  void set_shape_fill_mode(ShapeFillMode mode) override { fill_mode_ = mode; sync_mode_combo(); }

private:
  void preview();
  void finish();
  void clear_preview();
  void sync_mode_combo();

  bool drawing_ = false;
  std::vector<int> xs_;
  std::vector<int> ys_;
  unsigned button_ = 1;
  int thickness_ = 1;
  ShapeFillMode fill_mode_ = ShapeFillMode::Stroke;
  Rect dirty_{};
  const char* id_ = "freeform";
  const char* name_ = "Freeform";
  std::unique_ptr<Gtk::Box> options_;
  Gtk::ComboBoxText* mode_combo_{nullptr};
};

Gtk::Widget* FreeformShapeTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    auto* label = Gtk::manage(new Gtk::Label("Mode"));
    mode_combo_ = Gtk::manage(new Gtk::ComboBoxText());
    mode_combo_->append("stroke", "Stroke");
    mode_combo_->append("fill", "Fill");
    mode_combo_->append("both", "Stroke and fill");
    sync_mode_combo();
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
    options_->show_all();
  }
  return options_.get();
}

void FreeformShapeTool::sync_mode_combo() {
  if (mode_combo_ == nullptr) {
    return;
  }
  if (fill_mode_ == ShapeFillMode::Fill) {
    mode_combo_->set_active_id("fill");
  } else if (fill_mode_ == ShapeFillMode::Both) {
    mode_combo_->set_active_id("both");
  } else {
    mode_combo_->set_active_id("stroke");
  }
}

void FreeformShapeTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (!ensure_editable()) {
    return;
  }
  host_->document().commit_floating();
  drawing_ = true;
  button_ = event.button;
  xs_.clear();
  ys_.clear();
  dirty_ = {};
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  xs_.push_back(x);
  ys_.push_back(y);
  host_->document().layers().copy_active_to_tool();
  preview();
}

void FreeformShapeTool::on_motion(CanvasEvent event) {
  if (!drawing_ || host_ == nullptr) {
    return;
  }
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  if (!xs_.empty() && xs_.back() == x && ys_.back() == y) {
    return;
  }
  xs_.push_back(x);
  ys_.push_back(y);
  preview();
}

void FreeformShapeTool::on_release(CanvasEvent /*event*/) {
  if (!drawing_) {
    return;
  }
  finish();
}

void FreeformShapeTool::on_cancel() {
  if (!drawing_) {
    return;
  }
  clear_preview();
  drawing_ = false;
  xs_.clear();
  ys_.clear();
}

void FreeformShapeTool::preview() {
  if (host_ == nullptr || xs_.size() < 2) {
    return;
  }
  Document& doc = host_->document();
  const Layer& active = doc.layers().active_layer();
  Layer& tool = doc.layers().tool_layer();
  const Rect previous = dirty_;
  restore_shape_preview(tool, active, previous);
  std::vector<int> xs = xs_;
  std::vector<int> ys = ys_;
  const int ox = active.offset_x();
  const int oy = active.offset_y();
  for (int& px : xs) {
    px -= ox;
  }
  for (int& py : ys) {
    py -= oy;
  }
  dirty_ = {};
  draw_polygon(tool.pixels(), tool.width(), tool.height(), tool.stride(), xs.data(), ys.data(),
               static_cast<int>(xs.size()), (host_ != nullptr ? host_->stroke_size() : thickness_),
               stroke_color(button_), fill_mode_, false, &dirty_);
  host_->invalidate_canvas(
      layer_dirty_to_canvas(active, rect_union(previous, dirty_)));
}

void FreeformShapeTool::finish() {
  drawing_ = false;
  if (host_ == nullptr) {
    xs_.clear();
    ys_.clear();
    return;
  }
  if (xs_.size() < 3) {
    clear_preview();
    xs_.clear();
    ys_.clear();
    return;
  }
  Document& doc = host_->document();
  auto cmd = PixelPatchCommand::from_layers(doc.layers().active_layer(), doc.layers().tool_layer(),
                                            dirty_, "Freeform", doc.layers().active_index());
  doc.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    doc.commit(std::move(cmd));
  } else {
    host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  }
  xs_.clear();
  ys_.clear();
  dirty_ = {};
}

void FreeformShapeTool::clear_preview() {
  if (host_ == nullptr) {
    return;
  }
  host_->document().layers().clear_tool_layer();
  host_->invalidate_canvas(layer_dirty_to_canvas(host_->document().layers().active_layer(), dirty_));
  dirty_ = {};
}

Tool* create_freeform_tool() {
  return new FreeformShapeTool(ShapeFillMode::Stroke, "freeform", "Freeform");
}

Tool* create_freeform_fill_tool() {
  return new FreeformShapeTool(ShapeFillMode::Fill, "freeform-fill", "Freeform fill");
}

}  // namespace lundukepaint
