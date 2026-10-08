// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/selection.hpp"

#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "raster/blend.hpp"

#include <algorithm>
#include <cstring>

namespace lundukepaint {

void Selection::bump() {
  ++generation_;
}

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
    return float_covers(x - float_x_, y - float_y_);
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
  source_layer_ = -1;
  bump();
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
  bump();
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
  bump();
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
  bump();
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
  bump();
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

bool Selection::float_covers(int local_x, int local_y) const {
  if (!floating_ || local_x < 0 || local_y < 0 || local_x >= float_w_ || local_y >= float_h_) {
    return false;
  }
  if (float_coverage_.empty()) {
    return true;
  }
  return float_coverage_[static_cast<std::size_t>(local_y) * static_cast<std::size_t>(float_w_) +
                         static_cast<std::size_t>(local_x)] != 0;
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

bool Selection::lift(const Layer& layer, int source_index) {
  if (empty_ || inverted_) {
    return false;
  }
  const Rect layer_canvas{layer.offset_x(), layer.offset_y(), layer.width(), layer.height()};
  const Rect canvas_r = rect_intersect(rect_, layer_canvas);
  if (canvas_r.empty()) {
    return false;
  }
  const Rect layer_r{canvas_r.x - layer.offset_x(), canvas_r.y - layer.offset_y(), canvas_r.w,
                     canvas_r.h};
  float_w_ = layer_r.w;
  float_h_ = layer_r.h;
  float_x_ = canvas_r.x;
  float_y_ = canvas_r.y;
  origin_x_ = canvas_r.x;
  origin_y_ = canvas_r.y;
  origin_w_ = canvas_r.w;
  origin_h_ = canvas_r.h;
  float_pixels_.assign(static_cast<std::size_t>(float_w_) * static_cast<std::size_t>(float_h_) * 4, 0);
  layer.read_rect(layer_r, float_pixels_.data());
  float_coverage_.assign(static_cast<std::size_t>(float_w_) * static_cast<std::size_t>(float_h_), 1);
  bool partial = false;
  if (!mask_.empty()) {
    for (int y = 0; y < float_h_; ++y) {
      for (int x = 0; x < float_w_; ++x) {
        const bool covered = mask_at(canvas_r.x + x, canvas_r.y + y);
        float_coverage_[static_cast<std::size_t>(y) * static_cast<std::size_t>(float_w_) +
                        static_cast<std::size_t>(x)] = covered ? 1 : 0;
        if (!covered) {
          partial = true;
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
  if (!partial) {
    float_coverage_.clear();
    mask_.clear();
    mask_w_ = 0;
    mask_h_ = 0;
  } else {
    mask_ = float_coverage_;
    mask_w_ = float_w_;
    mask_h_ = float_h_;
  }
  floating_ = true;
  source_layer_ = source_index;
  rect_ = float_rect();
  bump();
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
  float_coverage_.clear();
  mask_.clear();
  mask_w_ = 0;
  mask_h_ = 0;
  rect_ = float_rect();
  bump();
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
  // The coverage was sampled on the old grid; a resampled float is rectangular.
  if (w != mask_w_ || h != mask_h_) {
    float_coverage_.clear();
    mask_.clear();
    mask_w_ = 0;
    mask_h_ = 0;
  }
  rect_ = float_rect();
  bump();
}

void Selection::move_float(int x, int y) {
  if (!floating_) {
    return;
  }
  float_x_ = x;
  float_y_ = y;
  rect_ = float_rect();
  bump();
}

SelectionState Selection::capture() const {
  SelectionState state;
  state.empty = empty_;
  state.inverted = inverted_;
  state.floating = floating_;
  state.transparent_move = transparent_move_;
  state.copy_mode = copy_mode_;
  state.rect = rect_;
  state.float_x = float_x_;
  state.float_y = float_y_;
  state.float_w = float_w_;
  state.float_h = float_h_;
  state.origin_x = origin_x_;
  state.origin_y = origin_y_;
  state.origin_w = origin_w_;
  state.origin_h = origin_h_;
  state.float_pixels = float_pixels_;
  state.float_coverage = float_coverage_;
  state.mask = mask_;
  state.mask_w = mask_w_;
  state.mask_h = mask_h_;
  state.source_layer = source_layer_;
  return state;
}

void Selection::restore(const SelectionState& state) {
  empty_ = state.empty;
  inverted_ = state.inverted;
  floating_ = state.floating;
  transparent_move_ = state.transparent_move;
  copy_mode_ = state.copy_mode;
  rect_ = state.rect;
  float_x_ = state.float_x;
  float_y_ = state.float_y;
  float_w_ = state.float_w;
  float_h_ = state.float_h;
  origin_x_ = state.origin_x;
  origin_y_ = state.origin_y;
  origin_w_ = state.origin_w;
  origin_h_ = state.origin_h;
  float_pixels_ = state.float_pixels;
  float_coverage_ = state.float_coverage;
  mask_ = state.mask;
  mask_w_ = state.mask_w;
  mask_h_ = state.mask_h;
  source_layer_ = state.source_layer;
  bump();
}

void Selection::note_layer_inserted(int index) {
  if (!floating_ || source_layer_ < 0 || index < 0) {
    return;
  }
  if (source_layer_ >= index) {
    ++source_layer_;
  }
}

void Selection::note_layer_removed(int index) {
  if (!floating_ || source_layer_ < 0 || index < 0) {
    return;
  }
  if (source_layer_ == index) {
    source_layer_ = -1;
  } else if (source_layer_ > index) {
    --source_layer_;
  }
}

void Selection::note_layer_moved(int from, int to) {
  if (!floating_ || source_layer_ < 0 || from == to) {
    return;
  }
  if (source_layer_ == from) {
    source_layer_ = to;
    return;
  }
  if (from < to) {
    if (source_layer_ > from && source_layer_ <= to) {
      --source_layer_;
    }
  } else if (source_layer_ >= to && source_layer_ < from) {
    ++source_layer_;
  }
}

void Selection::note_stack_flattened() {
  if (floating_) {
    source_layer_ = 0;
  }
}

void Selection::drop_float() {
  floating_ = false;
  copy_mode_ = false;
  float_pixels_.clear();
  float_coverage_.clear();
  float_w_ = 0;
  float_h_ = 0;
  origin_w_ = 0;
  origin_h_ = 0;
  source_layer_ = -1;
  bump();
}

void clip_rect_to_selection(Layer& dest, const Layer& source, Rect rect, const Selection& sel) {
  if (sel.empty()) {
    return;
  }
  rect = rect_intersect(rect, Rect{0, 0, dest.width(), dest.height()});
  rect = rect_intersect(rect, Rect{0, 0, source.width(), source.height()});
  if (rect.empty() || dest.stride() < dest.width() * 4 || source.stride() < source.width() * 4) {
    return;
  }
  const int ox = source.offset_x();
  const int oy = source.offset_y();
  bool restored = false;
  for (int y = rect.y; y < rect.y2(); ++y) {
    int x = rect.x;
    const int x1 = rect.x2();
    while (x < x1) {
      if (sel.contains(x + ox, y + oy)) {
        ++x;
        continue;
      }
      const int x0 = x;
      while (x < x1 && !sel.contains(x + ox, y + oy)) {
        ++x;
      }
      const int n = x - x0;
      std::uint8_t* d = dest.pixels() + static_cast<std::size_t>(y) * dest.stride() +
                        static_cast<std::size_t>(x0) * 4;
      const std::uint8_t* s = source.pixels() + static_cast<std::size_t>(y) * source.stride() +
                              static_cast<std::size_t>(x0) * 4;
      std::memcpy(d, s, static_cast<std::size_t>(n) * 4);
      restored = true;
    }
  }
  if (restored) {
    dest.invalidate_thumbnail();
  }
}

namespace {

Rect layer_canvas_rect(const Layer& layer) {
  return {layer.offset_x(), layer.offset_y(), layer.width(), layer.height()};
}

Rect canvas_to_layer_rect(const Layer& layer, Rect canvas) {
  if (canvas.empty()) {
    return {};
  }
  return {canvas.x - layer.offset_x(), canvas.y - layer.offset_y(), canvas.w, canvas.h};
}

}  // namespace

void fill_selection(Layer& layer, const Selection& sel, Color color, Rect* dirty) {
  if (dirty != nullptr) {
    *dirty = {};
  }
  if (sel.empty()) {
    return;
  }
  const Rect layer_bounds{0, 0, layer.width(), layer.height()};
  if (sel.floating()) {
    const Rect canvas_r = rect_intersect(sel.float_rect(), layer_canvas_rect(layer));
    const Rect r = rect_intersect(canvas_to_layer_rect(layer, canvas_r), layer_bounds);
    if (r.empty()) {
      return;
    }
    if (!sel.has_float_coverage()) {
      layer.fill_rect(r, color);
      if (dirty != nullptr) {
        *dirty = r;
      }
      return;
    }
    int minx = layer.width();
    int miny = layer.height();
    int maxx = -1;
    int maxy = -1;
    for (int y = 0; y < sel.float_h(); ++y) {
      for (int x = 0; x < sel.float_w(); ++x) {
        if (!sel.float_covers(x, y)) {
          continue;
        }
        const int lx = sel.float_x() - layer.offset_x() + x;
        const int ly = sel.float_y() - layer.offset_y() + y;
        if (lx < 0 || ly < 0 || lx >= layer.width() || ly >= layer.height()) {
          continue;
        }
        layer.set_pixel(lx, ly, color);
        if (lx < minx) {
          minx = lx;
        }
        if (ly < miny) {
          miny = ly;
        }
        if (lx > maxx) {
          maxx = lx;
        }
        if (ly > maxy) {
          maxy = ly;
        }
      }
    }
    if (dirty != nullptr && maxx >= minx) {
      *dirty = {minx, miny, maxx - minx + 1, maxy - miny + 1};
    }
    return;
  }
  if (!sel.inverted() && !sel.has_mask()) {
    const Rect canvas_r = rect_intersect(sel.bounds(), layer_canvas_rect(layer));
    const Rect r = rect_intersect(canvas_to_layer_rect(layer, canvas_r), layer_bounds);
    if (r.empty()) {
      return;
    }
    layer.fill_rect(r, color);
    if (dirty != nullptr) {
      *dirty = r;
    }
    return;
  }
  int minx = layer.width();
  int miny = layer.height();
  int maxx = -1;
  int maxy = -1;
  const int ox = layer.offset_x();
  const int oy = layer.offset_y();
  for (int y = 0; y < layer.height(); ++y) {
    for (int x = 0; x < layer.width(); ++x) {
      if (!sel.contains(x + ox, y + oy)) {
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
        const Color c = layer.pixel(x - layer.offset_x(), y - layer.offset_y());
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
  const Rect canvas_r = rect_intersect(sel.bounds(), layer_canvas_rect(layer));
  const Rect r = rect_intersect(canvas_to_layer_rect(layer, canvas_r),
                                Rect{0, 0, layer.width(), layer.height()});
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
        if (!sel.mask_at(canvas_r.x + x, canvas_r.y + y)) {
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
               bool skip_transparent, const std::uint8_t* coverage) {
  if (src == nullptr || sw < 1 || sh < 1) {
    return;
  }
  for (int y = 0; y < sh; ++y) {
    const int dyi = dy + y;
    if (dyi < 0 || dyi >= dest.height()) {
      continue;
    }
    const std::uint8_t* srow = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(sstride);
    const std::uint8_t* crow =
        coverage == nullptr ? nullptr
                            : coverage + static_cast<std::size_t>(y) * static_cast<std::size_t>(sw);
    for (int x = 0; x < sw; ++x) {
      if (crow != nullptr && crow[x] == 0) {
        continue;
      }
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

void paint_floating_selection(const LayerStack& layers, const Selection& sel, std::uint8_t* dest,
                              int dest_stride, Rect view, bool substitute_clear, Color hole_clear,
                              Color float_clear, const std::uint8_t* hole_rgba, int hole_stride) {
  if (dest == nullptr || view.empty() || !sel.floating() || sel.float_pixels() == nullptr) {
    return;
  }
  int skip = sel.source_layer();
  if (skip < 0 || skip >= layers.count()) {
    skip = layers.active_index();
  }
  auto pixel_at = [&](int x, int y) -> std::uint8_t* {
    const int lx = x - view.x;
    const int ly = y - view.y;
    if (lx < 0 || ly < 0 || lx >= view.w || ly >= view.h) {
      return nullptr;
    }
    return dest + static_cast<std::size_t>(ly) * static_cast<std::size_t>(dest_stride) +
           static_cast<std::size_t>(lx) * 4;
  };
  if (!sel.copy_mode()) {
    const Rect origin = sel.origin_rect();
    const Rect hole = rect_intersect(origin, view);
    if (!hole.empty()) {
      std::vector<std::uint8_t> below_store;
      const std::uint8_t* below = hole_rgba;
      int below_stride = hole_stride;
      int below_origin_x = origin.x;
      int below_origin_y = origin.y;
      if (below == nullptr || below_stride < origin.w * 4) {
        below_store.assign(static_cast<std::size_t>(hole.w) * static_cast<std::size_t>(hole.h) * 4, 0);
        layers.composite_rect(below_store.data(), hole.w * 4, hole, nullptr, -1, skip);
        below = below_store.data();
        below_stride = hole.w * 4;
        below_origin_x = hole.x;
        below_origin_y = hole.y;
      }
      for (int y = 0; y < hole.h; ++y) {
        for (int x = 0; x < hole.w; ++x) {
          const int canvas_x = hole.x + x;
          const int canvas_y = hole.y + y;
          if (!sel.float_covers(canvas_x - origin.x, canvas_y - origin.y)) {
            continue;
          }
          std::uint8_t* d = pixel_at(canvas_x, canvas_y);
          if (d == nullptr) {
            continue;
          }
          const int sx = canvas_x - below_origin_x;
          const int sy = canvas_y - below_origin_y;
          const std::uint8_t* s =
              below + static_cast<std::size_t>(sy) * static_cast<std::size_t>(below_stride) +
              static_cast<std::size_t>(sx) * 4;
          if (s[3] == 0 && substitute_clear) {
            d[0] = hole_clear.r;
            d[1] = hole_clear.g;
            d[2] = hole_clear.b;
            d[3] = hole_clear.a;
          } else {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
          }
        }
      }
    }
  }
  const Rect fr = sel.float_rect();
  for (int y = 0; y < fr.h; ++y) {
    const std::uint8_t* srow =
        sel.float_pixels() + static_cast<std::size_t>(y) * static_cast<std::size_t>(fr.w) * 4;
    for (int x = 0; x < fr.w; ++x) {
      std::uint8_t* d = pixel_at(fr.x + x, fr.y + y);
      if (d == nullptr) {
        continue;
      }
      if (!sel.float_covers(x, y)) {
        continue;
      }
      const std::uint8_t* s = srow + static_cast<std::size_t>(x) * 4;
      if (s[3] == 0) {
        if (!sel.transparent_move() && d[3] == 0 && substitute_clear) {
          d[0] = float_clear.r;
          d[1] = float_clear.g;
          d[2] = float_clear.b;
          d[3] = float_clear.a;
        }
        continue;
      }
      if (s[3] == 255) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = s[3];
      } else {
        blend_pixel(d, s, BlendMode::Normal, 1.0f);
      }
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
  const Rect canvas{0, 0, canvas_w, canvas_h};
  Rect region = canvas;
  if (!sel.empty() && !sel.inverted()) {
    region = rect_intersect(sel.bounds(), canvas);
    if (region.empty()) {
      return;
    }
  }
  std::vector<std::uint8_t> merged(static_cast<std::size_t>(region.w) * static_cast<std::size_t>(region.h) *
                                   4, 0);
  layers.composite_rect(merged.data(), region.w * 4, region);
  if (sel.floating()) {
    paint_floating_selection(layers, sel, merged.data(), region.w * 4, region, false,
                             Color::transparent(), Color::transparent());
  }

  auto sample = [&](int x, int y) {
    const int lx = x - region.x;
    const int ly = y - region.y;
    const std::uint8_t* p =
        merged.data() + (static_cast<std::size_t>(ly) * static_cast<std::size_t>(region.w) +
                         static_cast<std::size_t>(lx)) *
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
  out_w = region.w;
  out_h = region.h;
  out.assign(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0);
  for (int y = 0; y < out_h; ++y) {
    for (int x = 0; x < out_w; ++x) {
      const int cx = region.x + x;
      const int cy = region.y + y;
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
