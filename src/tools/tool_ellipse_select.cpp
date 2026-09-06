// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/selection_xform.hpp"

#include "doc/document.hpp"
#include "raster/shapes.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace lundukepaint {

class EllipseSelectTool : public Tool {
public:
  const char* id() const override { return "ellipse-select"; }
  const char* name() const override { return "Ellipse select"; }
  char shortcut() const override { return 'I'; }
  const char* hint() const override {
    return "Ellipse select: drag; Shift makes a circle; drag inside to move";
  }
  bool is_stroking() const override { return dragging_ || moving_ || xform_.active(); }
  Gtk::Widget* options_widget() override;

  void on_press(CanvasEvent event) override;
  void on_motion(CanvasEvent event) override;
  void on_release(CanvasEvent event) override;
  void on_cancel() override;

private:
  int clamp_x(int x) const;
  int clamp_y(int y) const;
  void apply_mask(int x1, int y1, bool constrain);
  void apply_transparent_option();

  bool dragging_ = false;
  bool moving_ = false;
  int start_x_ = 0;
  int start_y_ = 0;
  int grab_dx_ = 0;
  int grab_dy_ = 0;
  Rect prev_dirty_{};
  std::unique_ptr<Gtk::Box> options_;
  Gtk::CheckButton* transparent_{nullptr};
  SelectionXform xform_;
};

Gtk::Widget* EllipseSelectTool::options_widget() {
  if (!options_) {
    options_ = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_HORIZONTAL, 8);
    transparent_ = Gtk::manage(new Gtk::CheckButton("Transparent move"));
    transparent_->set_active(false);
    transparent_->signal_toggled().connect([this]() { apply_transparent_option(); });
    options_->pack_start(*transparent_, Gtk::PACK_SHRINK);
    options_->show_all();
  }
  return options_.get();
}

void EllipseSelectTool::apply_transparent_option() {
  if (host_ == nullptr || transparent_ == nullptr) {
    return;
  }
  host_->document().selection().set_transparent_move(transparent_->get_active());
  host_->invalidate_canvas(host_->document().selection().dirty_union());
}

int EllipseSelectTool::clamp_x(int x) const {
  if (host_ == nullptr) {
    return x;
  }
  return std::clamp(x, 0, host_->document().width() - 1);
}

int EllipseSelectTool::clamp_y(int y) const {
  if (host_ == nullptr) {
    return y;
  }
  return std::clamp(y, 0, host_->document().height() - 1);
}

void EllipseSelectTool::apply_mask(int x1, int y1, bool constrain) {
  if (host_ == nullptr) {
    return;
  }
  if (constrain) {
    constrain_square(start_x_, start_y_, &x1, &y1);
  }
  x1 = clamp_x(x1);
  y1 = clamp_y(y1);
  Rect r = Rect::from_points(start_x_, start_y_, x1, y1);
  Selection& sel = host_->document().selection();
  const Rect before = sel.bounds();
  if (r.w < 2 || r.h < 2) {
    sel.set_rect(r);
  } else {
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(r.w) * static_cast<std::size_t>(r.h), 0);
    fill_ellipse_mask(mask.data(), r.w, r.h);
    sel.set_mask(r, std::move(mask));
  }
  host_->invalidate_canvas(rect_union(before, sel.bounds()));
  host_->document().notify_changed();
}

void EllipseSelectTool::on_press(CanvasEvent event) {
  if (host_ == nullptr || (event.button != 1 && event.button != 3)) {
    return;
  }
  if (xform_.on_press(host_, event, host_->canvas_zoom())) {
    return;
  }
  Document& doc = host_->document();
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  Selection& sel = doc.selection();
  apply_transparent_option();

  const bool ctrl = (event.modifiers & Modifier::Ctrl) != 0;
  const bool can_move = !sel.empty() && !sel.inverted() &&
                        (sel.floating() ? sel.float_rect().contains(x, y) : sel.contains(x, y));
  if (can_move) {
    if (doc.layers().active_layer().locked()) {
      host_->show_status_hint("Layer is locked");
      return;
    }
    if (!sel.floating()) {
      sel.lift(doc.layers().active_layer());
    }
    sel.set_copy_mode(sel.copy_mode() || ctrl);
    moving_ = true;
    dragging_ = false;
    grab_dx_ = x - sel.float_x();
    grab_dy_ = y - sel.float_y();
    prev_dirty_ = sel.dirty_union();
    host_->invalidate_canvas(prev_dirty_);
    doc.notify_changed();
    return;
  }

  doc.commit_floating();
  dragging_ = true;
  moving_ = false;
  start_x_ = clamp_x(x);
  start_y_ = clamp_y(y);
  prev_dirty_ = sel.bounds();
  apply_mask(start_x_, start_y_, false);
}

void EllipseSelectTool::on_motion(CanvasEvent event) {
  if (host_ == nullptr) {
    return;
  }
  if (xform_.active()) {
    xform_.on_motion(host_, event);
    return;
  }
  Document& doc = host_->document();
  Selection& sel = doc.selection();
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  if (moving_ && sel.floating()) {
    if ((event.modifiers & Modifier::Ctrl) != 0) {
      sel.set_copy_mode(true);
    }
    const Rect before = sel.dirty_union();
    sel.move_float(x - grab_dx_, y - grab_dy_);
    host_->invalidate_canvas(rect_union(before, sel.dirty_union()));
    doc.notify_changed();
    return;
  }
  if (dragging_) {
    apply_mask(x, y, (event.modifiers & Modifier::Shift) != 0);
  }
}

void EllipseSelectTool::on_release(CanvasEvent event) {
  if (xform_.active()) {
    xform_.on_release(host_);
    return;
  }
  if (moving_) {
    moving_ = false;
    if (host_ != nullptr && (event.modifiers & Modifier::Ctrl) != 0) {
      host_->document().selection().set_copy_mode(true);
    }
    if (host_ != nullptr) {
      host_->document().notify_invalidated(host_->document().selection().dirty_union());
    }
    return;
  }
  if (dragging_) {
    apply_mask(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)),
               (event.modifiers & Modifier::Shift) != 0);
    Selection& sel = host_->document().selection();
    if (sel.bounds().w < 2 && sel.bounds().h < 2) {
      const Rect dirty = sel.bounds();
      sel.clear();
      host_->invalidate_canvas(dirty);
      host_->document().notify_changed();
    }
    dragging_ = false;
  }
}

void EllipseSelectTool::on_cancel() {
  if (xform_.active()) {
    xform_.on_cancel(host_);
    return;
  }
  if (host_ == nullptr) {
    dragging_ = false;
    moving_ = false;
    return;
  }
  if (moving_) {
    Selection& sel = host_->document().selection();
    const Rect dirty = sel.dirty_union();
    sel.move_float(sel.origin_x(), sel.origin_y());
    moving_ = false;
    host_->invalidate_canvas(dirty);
    return;
  }
  if (dragging_) {
    host_->document().selection().clear();
    dragging_ = false;
    host_->invalidate_canvas(prev_dirty_);
  }
}

Tool* create_ellipse_select_tool() {
  return new EllipseSelectTool();
}

}  // namespace lundukepaint
