// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"
#include "tools/selection_xform.hpp"

#include "doc/document.hpp"

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace lundukepaint {

class RectSelectTool : public Tool {
public:
  const char* id() const override { return "rect-select"; }
  const char* name() const override { return "Rectangle select"; }
  char shortcut() const override { return 'S'; }
  const char* hint() const override {
    return "Select: drag a rectangle; drag inside to move; Ctrl copies";
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
  void finish_rubber();
  void apply_transparent_option();

  bool dragging_ = false;
  bool moving_ = false;
  int start_x_ = 0;
  int start_y_ = 0;
  int last_x_ = 0;
  int last_y_ = 0;
  int grab_dx_ = 0;
  int grab_dy_ = 0;
  Rect prev_dirty_{};
  std::unique_ptr<Gtk::Box> options_;
  Gtk::CheckButton* transparent_{nullptr};
  SelectionXform xform_;
};

Gtk::Widget* RectSelectTool::options_widget() {
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

void RectSelectTool::apply_transparent_option() {
  if (host_ == nullptr || transparent_ == nullptr) {
    return;
  }
  host_->document().selection().set_transparent_move(transparent_->get_active());
  host_->invalidate_canvas(host_->document().selection().dirty_union());
}

int RectSelectTool::clamp_x(int x) const {
  if (host_ == nullptr) {
    return x;
  }
  return std::clamp(x, 0, host_->document().width() - 1);
}

int RectSelectTool::clamp_y(int y) const {
  if (host_ == nullptr) {
    return y;
  }
  return std::clamp(y, 0, host_->document().height() - 1);
}

void RectSelectTool::on_press(CanvasEvent event) {
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
                        (sel.floating() ? sel.float_rect().contains(x, y) : sel.bounds().contains(x, y));
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
  last_x_ = start_x_;
  last_y_ = start_y_;
  prev_dirty_ = sel.bounds();
  sel.set_rect(Rect::from_points(start_x_, start_y_, start_x_, start_y_));
  host_->invalidate_canvas(rect_union(prev_dirty_, sel.bounds()));
  doc.notify_changed();
}

void RectSelectTool::on_motion(CanvasEvent event) {
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
  if (!dragging_) {
    return;
  }
  last_x_ = clamp_x(x);
  last_y_ = clamp_y(y);
  const Rect before = sel.bounds();
  sel.set_rect(Rect::from_points(start_x_, start_y_, last_x_, last_y_));
  host_->invalidate_canvas(rect_union(before, sel.bounds()));
  doc.notify_changed();
}

void RectSelectTool::finish_rubber() {
  if (host_ == nullptr) {
    dragging_ = false;
    return;
  }
  Document& doc = host_->document();
  Selection& sel = doc.selection();
  Rect r = Rect::from_points(start_x_, start_y_, last_x_, last_y_);
  if (r.w < 2 && r.h < 2) {
    const Rect dirty = sel.bounds();
    sel.clear();
    dragging_ = false;
    host_->invalidate_canvas(dirty);
    doc.notify_changed();
    return;
  }
  sel.set_rect(r);
  dragging_ = false;
  host_->invalidate_canvas(r);
}

void RectSelectTool::on_release(CanvasEvent event) {
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
    last_x_ = clamp_x(static_cast<int>(std::floor(event.x)));
    last_y_ = clamp_y(static_cast<int>(std::floor(event.y)));
    finish_rubber();
  }
}

void RectSelectTool::on_cancel() {
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

Tool* create_rect_select_tool() {
  return new RectSelectTool();
}

}  // namespace lundukepaint
