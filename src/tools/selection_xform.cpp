// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/selection_xform.hpp"

#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/transform.hpp"

#include <algorithm>
#include <cmath>

namespace lundukepaint {
namespace {

int handle_hit_px(double zoom) {
  return std::max(3, static_cast<int>(std::lround(8.0 / std::max(zoom, 0.25))));
}

int rotate_offset(double zoom) {
  return std::max(10, static_cast<int>(std::lround(18.0 / std::max(zoom, 0.25))));
}

bool near_point(int x, int y, int px, int py, int r) {
  return std::abs(x - px) <= r && std::abs(y - py) <= r;
}

void handle_points(Rect r, int rot_off, int* xs, int* ys) {
  xs[0] = r.x;
  ys[0] = r.y;
  xs[1] = r.x + r.w / 2;
  ys[1] = r.y;
  xs[2] = r.x + r.w;
  ys[2] = r.y;
  xs[3] = r.x + r.w;
  ys[3] = r.y + r.h / 2;
  xs[4] = r.x + r.w;
  ys[4] = r.y + r.h;
  xs[5] = r.x + r.w / 2;
  ys[5] = r.y + r.h;
  xs[6] = r.x;
  ys[6] = r.y + r.h;
  xs[7] = r.x;
  ys[7] = r.y + r.h / 2;
  xs[8] = r.x + r.w / 2;
  ys[8] = r.y - rot_off;
}

}  // namespace

SelHandle hit_selection_handle(const Selection& sel, int canvas_x, int canvas_y, double zoom) {
  if (sel.empty() || sel.inverted()) {
    return SelHandle::None;
  }
  const Rect r = sel.bounds();
  if (r.empty()) {
    return SelHandle::None;
  }
  const int rad = handle_hit_px(zoom);
  const int rot = rotate_offset(zoom);
  int xs[9];
  int ys[9];
  handle_points(r, rot, xs, ys);
  static const SelHandle order[9] = {SelHandle::NW,     SelHandle::N,  SelHandle::NE,
                                     SelHandle::E,      SelHandle::SE, SelHandle::S,
                                     SelHandle::SW,     SelHandle::W,  SelHandle::Rotate};
  for (int i = 0; i < 9; ++i) {
    if (near_point(canvas_x, canvas_y, xs[i], ys[i], rad)) {
      return order[i];
    }
  }
  return SelHandle::None;
}

void draw_selection_handles(const Cairo::RefPtr<Cairo::Context>& cr, const Selection& sel, int ox,
                            int oy, double zoom) {
  if (sel.empty() || sel.inverted()) {
    return;
  }
  const Rect r = sel.bounds();
  if (r.empty()) {
    return;
  }
  const int rot = rotate_offset(zoom);
  int xs[9];
  int ys[9];
  handle_points(r, rot, xs, ys);
  const double nx = ox + xs[1] * zoom;
  const double ny = oy + ys[1] * zoom;
  const double rx = ox + xs[8] * zoom;
  const double ry = oy + ys[8] * zoom;
  cr->save();
  cr->set_line_width(1.0);
  cr->set_source_rgb(0.1, 0.1, 0.1);
  cr->move_to(nx, ny);
  cr->line_to(rx, ry);
  cr->stroke();
  for (int i = 0; i < 9; ++i) {
    const double hx = ox + xs[i] * zoom;
    const double hy = oy + ys[i] * zoom;
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->rectangle(hx - 3.5, hy - 3.5, 7.0, 7.0);
    cr->fill_preserve();
    cr->set_source_rgb(0.1, 0.1, 0.1);
    cr->stroke();
  }
  cr->restore();
}

bool SelectionXform::on_press(ToolHost* host, CanvasEvent event, double zoom) {
  if (host == nullptr || (event.button != 1 && event.button != 3)) {
    return false;
  }
  Document& doc = host->document();
  Selection& sel = doc.selection();
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  const SelHandle h = hit_selection_handle(sel, x, y, zoom);
  if (h == SelHandle::None) {
    return false;
  }
  if (doc.layers().active_layer().locked()) {
    host->show_status_hint("Layer is locked");
    return true;
  }
  if (!sel.floating()) {
    if (!sel.lift(doc.layers().active_layer(), doc.layers().active_index())) {
      return true;
    }
  }
  handle_ = h;
  start_rect_ = sel.float_rect();
  orig_w_ = sel.float_w();
  orig_h_ = sel.float_h();
  orig_pixels_.assign(sel.float_pixels(),
                      sel.float_pixels() + static_cast<std::size_t>(orig_w_) *
                                               static_cast<std::size_t>(orig_h_) * 4);
  orig_coverage_.clear();
  if (sel.has_float_coverage() && sel.float_coverage() != nullptr) {
    const std::size_t n = static_cast<std::size_t>(orig_w_) * static_cast<std::size_t>(orig_h_);
    orig_coverage_.assign(sel.float_coverage(), sel.float_coverage() + n);
  }
  if (h == SelHandle::Rotate) {
    mode_ = Mode::Rotate;
    const double cx = start_rect_.x + start_rect_.w * 0.5;
    const double cy = start_rect_.y + start_rect_.h * 0.5;
    start_angle_ = std::atan2(event.y - cy, event.x - cx);
    host->show_status_hint("Rotate selection");
  } else {
    mode_ = Mode::Scale;
    host->show_status_hint("Scale selection");
  }
  host->invalidate_canvas(sel.dirty_union());
  doc.notify_selection();
  return true;
}

void SelectionXform::on_motion(ToolHost* host, CanvasEvent event) {
  if (host == nullptr || mode_ == Mode::None) {
    return;
  }
  Document& doc = host->document();
  Selection& sel = doc.selection();
  const Rect before = sel.dirty_union();
  const int x = static_cast<int>(std::floor(event.x));
  const int y = static_cast<int>(std::floor(event.y));
  if (mode_ == Mode::Scale) {
    int nx = start_rect_.x;
    int ny = start_rect_.y;
    int nw = start_rect_.w;
    int nh = start_rect_.h;
    const int right = start_rect_.x + start_rect_.w;
    const int bottom = start_rect_.y + start_rect_.h;
    switch (handle_) {
      case SelHandle::E:
        nw = std::max(1, x - start_rect_.x);
        break;
      case SelHandle::W:
        nx = x;
        nw = std::max(1, right - nx);
        break;
      case SelHandle::S:
        nh = std::max(1, y - start_rect_.y);
        break;
      case SelHandle::N:
        ny = y;
        nh = std::max(1, bottom - ny);
        break;
      case SelHandle::SE:
        nw = std::max(1, x - start_rect_.x);
        nh = std::max(1, y - start_rect_.y);
        break;
      case SelHandle::SW:
        nx = x;
        nw = std::max(1, right - nx);
        nh = std::max(1, y - start_rect_.y);
        break;
      case SelHandle::NE:
        nw = std::max(1, x - start_rect_.x);
        ny = y;
        nh = std::max(1, bottom - ny);
        break;
      case SelHandle::NW:
        nx = x;
        ny = y;
        nw = std::max(1, right - nx);
        nh = std::max(1, bottom - ny);
        break;
      default:
        break;
    }
    if ((event.modifiers & Modifier::Shift) != 0 && orig_w_ > 0 && orig_h_ > 0) {
      const double aspect = static_cast<double>(orig_w_) / static_cast<double>(orig_h_);
      if (nw > nh) {
        nh = std::max(1, static_cast<int>(std::lround(nw / aspect)));
      } else {
        nw = std::max(1, static_cast<int>(std::lround(nh * aspect)));
      }
      if (handle_ == SelHandle::W || handle_ == SelHandle::NW || handle_ == SelHandle::SW) {
        nx = right - nw;
      }
      if (handle_ == SelHandle::N || handle_ == SelHandle::NW || handle_ == SelHandle::NE) {
        ny = bottom - nh;
      }
    }
    sel.scale_float_nearest(nx, ny, nw, nh, orig_pixels_.data(), orig_w_, orig_h_,
                            orig_coverage_.empty() ? nullptr : orig_coverage_.data());
  } else if (mode_ == Mode::Rotate) {
    const double cx = start_rect_.x + start_rect_.w * 0.5;
    const double cy = start_rect_.y + start_rect_.h * 0.5;
    const double ang = std::atan2(event.y - cy, event.x - cx) - start_angle_;
    int steps = static_cast<int>(std::lround(ang / (3.14159265358979323846 * 0.5)));
    steps %= 4;
    if (steps < 0) {
      steps += 4;
    }
    const int nx = static_cast<int>(std::lround(cx - (steps % 2 == 0 ? orig_w_ : orig_h_) * 0.5));
    const int ny = static_cast<int>(std::lround(cy - (steps % 2 == 0 ? orig_h_ : orig_w_) * 0.5));
    sel.rotate_float_steps(steps, nx, ny, orig_pixels_.data(), orig_w_, orig_h_,
                           orig_coverage_.empty() ? nullptr : orig_coverage_.data());
  }
  host->invalidate_canvas(rect_union(before, sel.dirty_union()));
  doc.notify_selection();
}

void SelectionXform::on_release(ToolHost* host) {
  if (host != nullptr && mode_ != Mode::None) {
    host->document().notify_invalidated(host->document().selection().dirty_union());
    host->document().notify_changed();
  }
  mode_ = Mode::None;
  orig_pixels_.clear();
  orig_coverage_.clear();
}

void SelectionXform::on_cancel(ToolHost* host) {
  if (host != nullptr && mode_ != Mode::None && !orig_pixels_.empty()) {
    Selection& sel = host->document().selection();
    const Rect dirty = sel.dirty_union();
    sel.transform_float(start_rect_.x, start_rect_.y, orig_w_, orig_h_, orig_pixels_, orig_coverage_);
    host->invalidate_canvas(rect_union(dirty, sel.dirty_union()));
  }
  mode_ = Mode::None;
  orig_pixels_.clear();
  orig_coverage_.clear();
}

}  // namespace lundukepaint
