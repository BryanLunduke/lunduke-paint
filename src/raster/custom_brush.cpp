// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/custom_brush.hpp"

#include "raster/transform.hpp"

#include <algorithm>
#include <cmath>

namespace lundukepaint {
namespace {

constexpr int kMaxSide = 256;

void put(std::uint8_t* p, Color c) {
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

}  // namespace

CustomBrush custom_brush_from_rgba(const std::uint8_t* rgba, int width, int height, int stride,
                                   Color background) {
  CustomBrush brush;
  if (rgba == nullptr || width < 1 || height < 1) {
    return brush;
  }
  int out_w = width;
  int out_h = height;
  if (out_w > kMaxSide || out_h > kMaxSide) {
    const double scale = static_cast<double>(kMaxSide) / static_cast<double>(std::max(out_w, out_h));
    out_w = std::max(1, static_cast<int>(width * scale));
    out_h = std::max(1, static_cast<int>(height * scale));
  }
  brush.width = out_w;
  brush.height = out_h;
  brush.rgba.assign(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0);
  for (int y = 0; y < out_h; ++y) {
    const int sy = std::min(height - 1, (y * height) / out_h);
    for (int x = 0; x < out_w; ++x) {
      const int sx = std::min(width - 1, (x * width) / out_w);
      const std::uint8_t* s = rgba + static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride) +
                              static_cast<std::size_t>(sx) * 4;
      Color c{s[0], s[1], s[2], s[3]};
      if (c.a == 0 || c == background) {
        c = Color::transparent();
      }
      put(brush.rgba.data() + (static_cast<std::size_t>(y) * out_w + x) * 4, c);
    }
  }
  return brush;
}

void stamp_custom_brush(std::uint8_t* rgba, int width, int height, int stride, int cx, int cy,
                        const CustomBrush& brush, Rect* dirty) {
  if (rgba == nullptr || brush.empty()) {
    return;
  }
  const int left = cx - brush.width / 2;
  const int top = cy - brush.height / 2;
  for (int y = 0; y < brush.height; ++y) {
    const int dy = top + y;
    if (dy < 0 || dy >= height) {
      continue;
    }
    for (int x = 0; x < brush.width; ++x) {
      const int dx = left + x;
      if (dx < 0 || dx >= width) {
        continue;
      }
      const std::uint8_t* s =
          brush.rgba.data() + (static_cast<std::size_t>(y) * brush.width + x) * 4;
      if (s[3] == 0) {
        continue;
      }
      put(rgba + static_cast<std::size_t>(dy) * stride + static_cast<std::size_t>(dx) * 4,
          {s[0], s[1], s[2], s[3]});
      if (dirty != nullptr) {
        *dirty = rect_union(*dirty, Rect{dx, dy, 1, 1});
      }
    }
  }
}

void stroke_custom_brush(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                         double x1, double y1, const CustomBrush& brush, Rect* dirty) {
  if (brush.empty()) {
    return;
  }
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double dist = std::hypot(dx, dy);
  const int span = std::max(brush.width, brush.height);
  const int step = brush.spacing > 0 ? brush.spacing : (span <= 24 ? 1 : std::max(1, span / 8));
  const int n = std::max(1, static_cast<int>(dist / step));
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n);
    const int cx = static_cast<int>(std::floor(x0 + dx * t + 0.5));
    const int cy = static_cast<int>(std::floor(y0 + dy * t + 0.5));
    stamp_custom_brush(rgba, width, height, stride, cx, cy, brush, dirty);
  }
}

void custom_brush_flip_h(CustomBrush& brush) {
  if (brush.empty()) {
    return;
  }
  flip_h(brush.rgba.data(), brush.width, brush.height, brush.width * 4);
}

void custom_brush_flip_v(CustomBrush& brush) {
  if (brush.empty()) {
    return;
  }
  flip_v(brush.rgba.data(), brush.width, brush.height, brush.width * 4);
}

void custom_brush_rotate_90(CustomBrush& brush) {
  if (brush.empty()) {
    return;
  }
  std::vector<std::uint8_t> dest(static_cast<std::size_t>(brush.width) * static_cast<std::size_t>(brush.height) * 4);
  rotate_90_cw(brush.rgba.data(), brush.width, brush.height, brush.width * 4, dest.data(), brush.height * 4);
  std::swap(brush.width, brush.height);
  brush.rgba = std::move(dest);
}

void custom_brush_scale(CustomBrush& brush, int numerator, int denominator) {
  if (brush.empty() || numerator < 1 || denominator < 1) {
    return;
  }
  int nw = std::max(1, brush.width * numerator / denominator);
  int nh = std::max(1, brush.height * numerator / denominator);
  constexpr int kLimit = 256;
  if (nw > kLimit || nh > kLimit) {
    const double fit = static_cast<double>(kLimit) / static_cast<double>(std::max(nw, nh));
    nw = std::max(1, static_cast<int>(nw * fit));
    nh = std::max(1, static_cast<int>(nh * fit));
  }
  std::vector<std::uint8_t> dest(static_cast<std::size_t>(nw) * static_cast<std::size_t>(nh) * 4);
  scale_nearest(brush.rgba.data(), brush.width, brush.height, brush.width * 4, dest.data(), nw, nh, nw * 4);
  brush.width = nw;
  brush.height = nh;
  brush.rgba = std::move(dest);
}

void custom_brush_remap(CustomBrush& brush, Color ink) {
  if (brush.empty()) {
    return;
  }
  for (int i = 0; i < brush.width * brush.height; ++i) {
    std::uint8_t* p = brush.rgba.data() + static_cast<std::size_t>(i) * 4;
    if (p[3] == 0) {
      continue;
    }
    p[0] = ink.r;
    p[1] = ink.g;
    p[2] = ink.b;
    p[3] = ink.a == 0 ? 0 : p[3];
  }
}

}  // namespace lundukepaint
