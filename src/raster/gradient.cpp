// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/gradient.hpp"

#include <algorithm>
#include <cmath>

namespace lundukepaint {
namespace {

// Ordered dither thresholds, 0..63. Classic 8×8 Bayer.
constexpr int kBayer[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

void put(std::uint8_t* p, Color c) {
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

}  // namespace

void gradient_dither(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                     int y1, Color c0, Color c1, GradientKind kind, const std::uint8_t* mask,
                     int mask_x, int mask_y, int mask_w, int mask_h, Rect* dirty) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return;
  }
  const double dx = static_cast<double>(x1 - x0);
  const double dy = static_cast<double>(y1 - y0);
  const double len2 = dx * dx + dy * dy;
  const double radius = std::sqrt(len2);
  int minx = width;
  int miny = height;
  int maxx = -1;
  int maxy = -1;
  const int y_lo = mask != nullptr ? std::max(0, mask_y) : 0;
  const int y_hi = mask != nullptr ? std::min(height, mask_y + mask_h) : height;
  const int x_lo = mask != nullptr ? std::max(0, mask_x) : 0;
  const int x_hi = mask != nullptr ? std::min(width, mask_x + mask_w) : width;
  for (int y = y_lo; y < y_hi; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = x_lo; x < x_hi; ++x) {
      if (mask != nullptr) {
        const int mx = x - mask_x;
        const int my = y - mask_y;
        if (mx < 0 || my < 0 || mx >= mask_w || my >= mask_h) {
          continue;
        }
        if (mask[static_cast<std::size_t>(my) * static_cast<std::size_t>(mask_w) +
                 static_cast<std::size_t>(mx)] == 0) {
          continue;
        }
      }
      double t = 0.0;
      if (kind == GradientKind::Radial) {
        if (radius >= 1.0) {
          const double dist = std::hypot(static_cast<double>(x - x0), static_cast<double>(y - y0));
          t = std::clamp(dist / radius, 0.0, 1.0);
        }
      } else if (len2 >= 1.0) {
        t = std::clamp(((x - x0) * dx + (y - y0) * dy) / len2, 0.0, 1.0);
      }
      const int threshold = kBayer[y & 7][x & 7];
      const int level = static_cast<int>(t * 64.0);
      put(row + static_cast<std::size_t>(x) * 4, level > threshold ? c1 : c0);
      minx = std::min(minx, x);
      miny = std::min(miny, y);
      maxx = std::max(maxx, x);
      maxy = std::max(maxy, y);
    }
  }
  if (dirty != nullptr && maxx >= minx) {
    *dirty = rect_union(*dirty, Rect{minx, miny, maxx - minx + 1, maxy - miny + 1});
  }
}

}  // namespace lundukepaint
