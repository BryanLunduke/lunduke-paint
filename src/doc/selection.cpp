// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/selection.hpp"

#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"

#include <algorithm>
#include <cstring>

namespace lundukepaint {

Rect Selection::bounds() const {
  if (empty_) {
    return {};
  }
  if (floating_) {
    return float_rect();
  }
  return rect_;
}

bool Selection::mask_at(int canvas_x, int canvas_y) const {
  if (mask_.empty() || !rect_.contains(canvas_x, canvas_y)) {
    return false;
  }
  const int mx = canvas_x - rect_.x;
  const int my = canvas_y - rect_.y;
  if (mx < 0 || my < 0 || mx >= mask_w_ || my >= mask_h_) {
    return false;
  }
  return mask_[static_cast<std::size_t>(my) * static_cast<std::size_t>(mask_w_) +
               static_cast<std::size_t>(mx)] != 0;
}

bool Selection::contains(int x, int y) const {
  if (empty_) {
    return false;
  }
  if (floating_) {
    return float_rect().contains(x, y);
  }
  bool inside = false;
  if (!mask_.empty()) {
    inside = mask_at(x, y);
  } else {
    inside = rect_.contains(x, y);
  }
  return inverted_ ? !inside : inside;
}

void Selection::clear() {
  empty_ = true;
  inverted_ = false;
  drop_float();
  rect_ = {};
  mask_.clear();
  mask_w_ = 0;
  mask_h_ = 0;
}

void Selection::set_rect(Rect rect) {
  if (rect.empty()) {
    clear();
    return;
  }
  empty_ = false;
  inverted_ = false;
  drop_float();
  rect_ = rect;
  mask_.clear();
  mask_w_ = 0;
  mask_h_ = 0;
}

void Selection::set_mask(Rect bounds, std::vector<std::uint8_t> mask) {
  if (bounds.empty() ||
      mask.size() < static_cast<std::size_t>(bounds.w) * static_cast<std::size_t>(bounds.h)) {
    clear();
    return;
  }
  empty_ = false;
  inverted_ = false;
  drop_float();
  rect_ = bounds;
  mask_w_ = bounds.w;
  mask_h_ = bounds.h;
  mask_ = std::move(mask);
}

void Selection::select_all(int width, int height) {
  if (width < 1 || height < 1) {
    clear();
    return;
  }
  empty_ = false;
  inverted_ = false;
  drop_float();
  rect_ = {0, 0, width, height};
  mask_.clear();
  mask_w_ = 0;
  mask_h_ = 0;
}

void Selection::invert(int width, int height) {
  drop_float();
  if (width < 1 || height < 1) {
    clear();
    return;
  }
  if (empty_) {
    select_all(width, height);
    return;
  }
  // Select All is a full-canvas rect with no mask; its inverse is empty.
  // A masked selection (magic wand, lasso) can use that same bounding box and
  // still has a complement, so the mask must keep the selection alive.
  const bool full_canvas =
      rect_.x == 0 && rect_.y == 0 && rect_.w == width && rect_.h == height;
  if (full_canvas && mask_.empty()) {
    clear();
    return;
  }
  inverted_ = !inverted_;
  empty_ = false;
}

Rect Selection::float_rect() const {
  if (!floating_ || float_w_ < 1 || float_h_ < 1) {
    return {};
  }
  return {float_x_, float_y_, float_w_, float_h_};
}

Rect Selection::origin_rect() const {
  if (!floating_ || origin_w_ < 1 || origin_h_ < 1) {
    return {};
  }
  return {origin_x_, origin_y_, origin_w_, origin_h_};
}

Rect Selection::dirty_union() const {
  return rect_union(origin_rect(), float_rect());
}

Color Selection::float_pixel(int x, int y) const {
  if (!floating_ || x < 0 || y < 0 || x >= float_w_ || y >= float_h_) {
    return Color::transparent();
  }
  const std::uint8_t* p =
      float_pixels_.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(float_w_) +
                              static_cast<std::size_t>(x)) *
                                 4;
  return {p[0], p[1], p[2], p[3]};
}

bool Selection::lift(const Layer& layer) {
  if (empty_ || inverted_) {
    return false;
  }
  Rect r = rect_intersect(rect_, Rect{0, 0, layer.width(), layer.height()});
  if (r.empty()) {
    return false;
  }
  float_w_ = r.w;
  float_h_ = r.h;
  float_x_ = r.x;
  float_y_ = r.y;
  origin_x_ = r.x;
  origin_y_ = r.y;
  origin_w_ = r.w;
  origin_h_ = r.h;
  float_pixels_.assign(static_cast<std::size_t>(float_w_) * static_cast<std::size_t>(float_h_) * 4, 0);
  layer.read_rect(r, float_pixels_.data());
  if (!mask_.empty()) {
    for (int y = 0; y < float_h_; ++y) {
      for (int x = 0; x < float_w_; ++x) {
        if (!mask_at(r.x + x, r.y + y)) {
          std::uint8_t* p =
              float_pixels_.data() +
              (static_cast<std::size_t>(y) * static_cast<std::size_t>(float_w_) +
               static_cast<std::size_t>(x)) *
                  4;
          p[0] = p[1] = p[2] = p[3] = 0;
        }
      }
    }
  }
  floating_ = true;
  rect_ = float_rect();
  return true;
}

void Selection::set_float_pixels(int x, int y, int w, int h, std::vector<std::uint8_t> rgba) {
  if (w < 1 || h < 1 ||
      rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
    clear();
    return;
  }
  empty_ = false;
  inverted_ = false;
  floating_ = true;
  copy_mode_ = true;
  float_x_ = x;
  float_y_ = y;
  float_w_ = w;
  float_h_ = h;
  origin_x_ = x;
  origin_y_ = y;
  origin_w_ = w;
  origin_h_ = h;
  float_pixels_ = std::move(rgba);
  rect_ = float_rect();
}

void Selection::transform_float(int x, int y, int w, int h, std::vector<std::uint8_t> rgba) {
  if (!floating_ || w < 1 || h < 1 ||
      rgba.size() < static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4) {
    return;
  }
  float_x_ = x;
  float_y_ = y;
  float_w_ = w;
  float_h_ = h;
  float_pixels_ = std::move(rgba);
  rect_ = float_rect();
}

void Selection::move_float(int x, int y) {
  if (!floating_) {
    return;
  }
  float_x_ = x;
  float_y_ = y;
  rect_ = float_rect();
}

void Selection::drop_float() {
  floating_ = false;
  copy_mode_ = false;
  float_pixels_.clear();
  float_w_ = 0;
  float_h_ = 0;
  origin_w_ = 0;
  origin_h_ = 0;
}

void clip_rect_to_selection(Layer& dest, const Layer& source, Rect rect, const Selection& sel) {
  if (sel.empty()) {
    return;
  }
  rect = rect_intersect(rect, Rect{0, 0, dest.width(), dest.height()});
  rect = rect_intersect(rect, Rect{0, 0, source.width(), source.height()});
  if (rect.empty()) {
    return;
  }
  const int ox = source.offset_x();
  const int oy = source.offset_y();
  for (int y = rect.y; y < rect.y2(); ++y) {
    for (int x = rect.x; x < rect.x2(); ++x) {
      if (!sel.contains(x + ox, y + oy)) {
        dest.set_pixel(x, y, source.pixel(x, y));
      }
    }
  }
}

void fill_selection(Layer& layer, const Selection& sel, Color color, Rect* dirty) {
  if (dirty != nullptr) {
    *dirty = {};
  }
  if (sel.empty()) {
    return;
  }
  int minx = layer.width();
  int miny = layer.height();
  int maxx = -1;
  int maxy = -1;
  if (sel.floating()) {
    const Rect r = rect_intersect(sel.float_rect(), Rect{0, 0, layer.width(), layer.height()});
    if (r.empty()) {
      return;
    }
    layer.fill_rect(r, color);
    if (dirty != nullptr) {
      *dirty = r;
    }
    return;
  }
  if (!sel.inverted() && !sel.has_mask()) {
    const Rect r = rect_intersect(sel.bounds(), Rect{0, 0, layer.width(), layer.height()});
    if (r.empty()) {
      return;
    }
    layer.fill_rect(r, color);
    if (dirty != nullptr) {
      *dirty = r;
    }
    return;
  }
  for (int y = 0; y < layer.height(); ++y) {
    for (int x = 0; x < layer.width(); ++x) {
      if (!sel.contains(x, y)) {
        continue;
      }
      layer.set_pixel(x, y, color);
      if (x < minx) {
        minx = x;
      }
      if (y < miny) {
        miny = y;
      }
      if (x > maxx) {
        maxx = x;
      }
      if (y > maxy) {
        maxy = y;
      }
    }
  }
  if (dirty != nullptr && maxx >= minx) {
    *dirty = {minx, miny, maxx - minx + 1, maxy - miny + 1};
  }
}

void copy_selection_rgba(const Layer& layer, const Selection& sel, int canvas_w, int canvas_h,
                         int& out_w, int& out_h, std::vector<std::uint8_t>& out) {
  out_w = 0;
  out_h = 0;
  out.clear();
  if (sel.floating() && sel.float_pixels() != nullptr) {
    out_w = sel.float_w();
    out_h = sel.float_h();
    out.assign(sel.float_pixels(),
               sel.float_pixels() + static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4);
    return;
  }
  if (sel.empty()) {
    return;
  }
  if (sel.inverted()) {
    out_w = canvas_w;
    out_h = canvas_h;
    out.assign(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0);
    for (int y = 0; y < canvas_h; ++y) {
      for (int x = 0; x < canvas_w; ++x) {
        if (!sel.contains(x, y)) {
          continue;
        }
        const Color c = layer.pixel(x, y);
        std::uint8_t* p =
            out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out_w) +
                          static_cast<std::size_t>(x)) *
                             4;
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = c.a;
      }
    }
    return;
  }
  Rect r = rect_intersect(sel.bounds(), Rect{0, 0, layer.width(), layer.height()});
  if (r.empty()) {
    return;
  }
  out_w = r.w;
  out_h = r.h;
  out.assign(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0);
  layer.read_rect(r, out.data());
  if (sel.has_mask()) {
    for (int y = 0; y < out_h; ++y) {
      for (int x = 0; x < out_w; ++x) {
        if (!sel.mask_at(r.x + x, r.y + y)) {
          std::uint8_t* p =
              out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out_w) +
                            static_cast<std::size_t>(x)) *
                               4;
          p[0] = p[1] = p[2] = p[3] = 0;
        }
      }
    }
  }
}

void blit_rgba(Layer& dest, int dx, int dy, const std::uint8_t* src, int sw, int sh, int sstride,
               bool skip_transparent) {
  if (src == nullptr || sw < 1 || sh < 1) {
    return;
  }
  for (int y = 0; y < sh; ++y) {
    const int dyi = dy + y;
    if (dyi < 0 || dyi >= dest.height()) {
      continue;
    }
    const std::uint8_t* srow = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(sstride);
    for (int x = 0; x < sw; ++x) {
      const int dxi = dx + x;
      if (dxi < 0 || dxi >= dest.width()) {
        continue;
      }
      const std::uint8_t* p = srow + static_cast<std::size_t>(x) * 4;
      if (skip_transparent && p[3] == 0) {
        continue;
      }
      dest.set_pixel(dxi, dyi, Color{p[0], p[1], p[2], p[3]});
    }
  }
}

void copy_merged_rgba(const LayerStack& layers, const Selection& sel, int canvas_w, int canvas_h,
                      int& out_w, int& out_h, std::vector<std::uint8_t>& out) {
  out_w = 0;
  out_h = 0;
  out.clear();
  if (canvas_w < 1 || canvas_h < 1) {
    return;
  }
  std::vector<std::uint8_t> merged(
      static_cast<std::size_t>(canvas_w) * static_cast<std::size_t>(canvas_h) * 4, 0);
  layers.composite_rect(merged.data(), canvas_w * 4, Rect{0, 0, canvas_w, canvas_h});
  if (sel.floating() && sel.float_pixels() != nullptr) {
    const Rect fr = sel.float_rect();
    for (int y = 0; y < fr.h; ++y) {
      const int dy = fr.y + y;
      if (dy < 0 || dy >= canvas_h) {
        continue;
      }
      const std::uint8_t* srow =
          sel.float_pixels() + static_cast<std::size_t>(y) * static_cast<std::size_t>(fr.w) * 4;
      for (int x = 0; x < fr.w; ++x) {
        const int dx = fr.x + x;
        if (dx < 0 || dx >= canvas_w) {
          continue;
        }
        const std::uint8_t* s = srow + static_cast<std::size_t>(x) * 4;
        if (sel.transparent_move() && s[3] == 0) {
          continue;
        }
        std::uint8_t* d =
            merged.data() + (static_cast<std::size_t>(dy) * static_cast<std::size_t>(canvas_w) +
                             static_cast<std::size_t>(dx)) *
                                4;
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
      }
    }
    if (!sel.copy_mode()) {
      const Rect hole = rect_intersect(sel.origin_rect(), Rect{0, 0, canvas_w, canvas_h});
      for (int y = hole.y; y < hole.y2(); ++y) {
        for (int x = hole.x; x < hole.x2(); ++x) {
          if (fr.contains(x, y) && sel.float_pixel(x - fr.x, y - fr.y).a != 0) {
            continue;
          }
          std::uint8_t* d =
              merged.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas_w) +
                               static_cast<std::size_t>(x)) *
                                  4;
          d[0] = d[1] = d[2] = d[3] = 0;
        }
      }
    }
  }

  auto sample = [&](int x, int y) {
    const std::uint8_t* p =
        merged.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas_w) +
                         static_cast<std::size_t>(x)) *
                            4;
    return Color{p[0], p[1], p[2], p[3]};
  };

  if (sel.empty()) {
    out_w = canvas_w;
    out_h = canvas_h;
    out = std::move(merged);
    return;
  }
  if (sel.inverted()) {
    out_w = canvas_w;
    out_h = canvas_h;
    out.assign(merged.size(), 0);
    for (int y = 0; y < canvas_h; ++y) {
      for (int x = 0; x < canvas_w; ++x) {
        if (!sel.contains(x, y)) {
          continue;
        }
        const Color c = sample(x, y);
        std::uint8_t* p =
            out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out_w) +
                          static_cast<std::size_t>(x)) *
                             4;
        p[0] = c.r;
        p[1] = c.g;
        p[2] = c.b;
        p[3] = c.a;
      }
    }
    return;
  }
  Rect r = rect_intersect(sel.bounds(), Rect{0, 0, canvas_w, canvas_h});
  if (r.empty()) {
    return;
  }
  out_w = r.w;
  out_h = r.h;
  out.assign(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0);
  for (int y = 0; y < out_h; ++y) {
    for (int x = 0; x < out_w; ++x) {
      const int cx = r.x + x;
      const int cy = r.y + y;
      if (sel.has_mask() && !sel.mask_at(cx, cy) && !sel.floating()) {
        continue;
      }
      if (!sel.floating() && !sel.contains(cx, cy)) {
        continue;
      }
      const Color c = sample(cx, cy);
      std::uint8_t* p =
          out.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(out_w) +
                        static_cast<std::size_t>(x)) *
                           4;
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
      p[3] = c.a;
    }
  }
}

}  // namespace lundukepaint
