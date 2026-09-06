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

class LassoTool : public Tool {
public:
  const char* id() const override { return "lasso"; }
  const char* name() const override { return "Freeform select"; }
  char shortcut() const override { return 'M'; }
  const char* hint() const override {
    return "Lasso: drag a freehand path; drag inside to move; Ctrl copies";
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
  void add_point(int x, int y);
  void preview_path();
  void finish_path();
  void apply_transparent_option();

  bool dragging_ = false;
  bool moving_ = false;
  std::vector<int> xs_;
  std::vector<int> ys_;
  int grab_dx_ = 0;
  int grab_dy_ = 0;
  Rect prev_dirty_{};
  std::unique_ptr<Gtk::Box> options_;
  Gtk::CheckButton* transparent_{nullptr};
  SelectionXform xform_;
};

Gtk::Widget* LassoTool::options_widget() {
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

void LassoTool::apply_transparent_option() {
  if (host_ == nullptr || transparent_ == nullptr) {
    return;
  }
  host_->document().selection().set_transparent_move(transparent_->get_active());
  host_->invalidate_canvas(host_->document().selection().dirty_union());
}

int LassoTool::clamp_x(int x) const {
  if (host_ == nullptr) {
    return x;
  }
  return std::clamp(x, 0, host_->document().width() - 1);
}

int LassoTool::clamp_y(int y) const {
  if (host_ == nullptr) {
    return y;
  }
  return std::clamp(y, 0, host_->document().height() - 1);
}

void LassoTool::add_point(int x, int y) {
  x = clamp_x(x);
  y = clamp_y(y);
  if (!xs_.empty() && xs_.back() == x && ys_.back() == y) {
    return;
  }
  xs_.push_back(x);
  ys_.push_back(y);
}

void LassoTool::preview_path() {
  if (host_ == nullptr || xs_.empty()) {
    return;
  }
  Selection& sel = host_->document().selection();
  const Rect before = sel.bounds();
  int minx = xs_[0];
  int miny = ys_[0];
  int maxx = xs_[0];
  int maxy = ys_[0];
  for (std::size_t i = 1; i < xs_.size(); ++i) {
    minx = std::min(minx, xs_[i]);
    miny = std::min(miny, ys_[i]);
    maxx = std::max(maxx, xs_[i]);
    maxy = std::max(maxy, ys_[i]);
  }
  const int w = maxx - minx + 1;
  const int h = maxy - miny + 1;
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
  std::vector<int> lxs = xs_;
  std::vector<int> lys = ys_;
  for (int& v : lxs) {
    v -= minx;
  }
  for (int& v : lys) {
    v -= miny;
  }
  stroke_polyline_mask(mask.data(), w, h, lxs.data(), lys.data(), static_cast<int>(lxs.size()),
                       false);
  sel.set_mask({minx, miny, w, h}, std::move(mask));
  host_->invalidate_canvas(rect_union(before, sel.bounds()));
  host_->document().notify_changed();
}

void LassoTool::finish_path() {
  if (host_ == nullptr) {
    xs_.clear();
    ys_.clear();
    dragging_ = false;
    return;
  }
  Selection& sel = host_->document().selection();
  if (xs_.size() < 3) {
    const Rect dirty = sel.bounds();
    sel.clear();
    xs_.clear();
    ys_.clear();
    dragging_ = false;
    host_->invalidate_canvas(dirty);
    host_->document().notify_changed();
    return;
  }
  int minx = xs_[0];
  int miny = ys_[0];
  int maxx = xs_[0];
  int maxy = ys_[0];
  for (std::size_t i = 1; i < xs_.size(); ++i) {
    minx = std::min(minx, xs_[i]);
    miny = std::min(miny, ys_[i]);
    maxx = std::max(maxx, xs_[i]);
    maxy = std::max(maxy, ys_[i]);
  }
  const int w = maxx - minx + 1;
  const int h = maxy - miny + 1;
  if (w < 2 && h < 2) {
    const Rect dirty = sel.bounds();
    sel.clear();
    xs_.clear();
    ys_.clear();
    dragging_ = false;
    host_->invalidate_canvas(dirty);
    host_->document().notify_changed();
    return;
  }
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
  std::vector<int> lxs = xs_;
  std::vector<int> lys = ys_;
  for (int& v : lxs) {
    v -= minx;
  }
  for (int& v : lys) {
    v -= miny;
  }
  fill_polygon_mask(mask.data(), w, h, lxs.data(), lys.data(), static_cast<int>(lxs.size()));
  stroke_polyline_mask(mask.data(), w, h, lxs.data(), lys.data(), static_cast<int>(lxs.size()),
                       true);
  const Rect before = sel.bounds();
  sel.set_mask({minx, miny, w, h}, std::move(mask));
  xs_.clear();
  ys_.clear();
  dragging_ = false;
  host_->invalidate_canvas(rect_union(before, sel.bounds()));
  host_->document().notify_changed();
}

void LassoTool::on_press(CanvasEvent event) {
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
  xs_.clear();
  ys_.clear();
  prev_dirty_ = sel.bounds();
  add_point(x, y);
  preview_path();
}

void LassoTool::on_motion(CanvasEvent event) {
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
    add_point(x, y);
    preview_path();
  }
}

void LassoTool::on_release(CanvasEvent event) {
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
    add_point(static_cast<int>(std::floor(event.x)), static_cast<int>(std::floor(event.y)));
    finish_path();
  }
}

void LassoTool::on_cancel() {
  if (xform_.active()) {
    xform_.on_cancel(host_);
    return;
  }
  if (host_ == nullptr) {
    dragging_ = false;
    moving_ = false;
    xs_.clear();
    ys_.clear();
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
    xs_.clear();
    ys_.clear();
    host_->invalidate_canvas(prev_dirty_);
  }
}

Tool* create_lasso_tool() {
  return new LassoTool();
}

}  // namespace lundukepaint
