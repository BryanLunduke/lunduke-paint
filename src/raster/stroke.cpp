// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/stroke.hpp"

#include <algorithm>
#include <cmath>

namespace lundukepaint {
namespace {

void put_replace(std::uint8_t* p, Color color) {
  p[0] = color.r;
  p[1] = color.g;
  p[2] = color.b;
  p[3] = color.a;
}

void put_blend(std::uint8_t* p, Color color, int coverage) {
  if (coverage <= 0) {
    return;
  }
  if (coverage >= 255 || color.a == 255) {
    if (coverage >= 255) {
      put_replace(p, color);
      return;
    }
  }
  const int src_a = (static_cast<int>(color.a) * coverage + 127) / 255;
  if (src_a <= 0) {
    return;
  }
  if (src_a >= 255) {
    put_replace(p, color);
    return;
  }
  const int dst_a = p[3];
  const int out_a = src_a + (dst_a * (255 - src_a) + 127) / 255;
  if (out_a <= 0) {
    p[0] = p[1] = p[2] = p[3] = 0;
    return;
  }
  auto ch = [&](int s, int d) {
    const int v = (s * src_a + (d * dst_a * (255 - src_a) + 127) / 255);
    return static_cast<std::uint8_t>(v / out_a);
  };
  p[0] = ch(color.r, p[0]);
  p[1] = ch(color.g, p[1]);
  p[2] = ch(color.b, p[2]);
  p[3] = static_cast<std::uint8_t>(out_a);
}

void grow(Rect* dirty, int x0, int y0, int x1, int y1, int width, int height) {
  if (dirty == nullptr) {
    return;
  }
  Rect added = rect_clamp(Rect{x0, y0, x1 - x0, y1 - y0}, width, height);
  *dirty = rect_union(*dirty, added);
}

}  // namespace

void stamp_square(std::uint8_t* rgba, int width, int height, int stride, int cx, int cy, int size,
                  Color color, Rect* dirty) {
  if (rgba == nullptr || size < 1) {
    return;
  }
  const int half = size / 2;
  const int x0 = cx - half;
  const int y0 = cy - half;
  const int x1 = x0 + size;
  const int y1 = y0 + size;
  const int ix0 = std::max(0, x0);
  const int iy0 = std::max(0, y0);
  const int ix1 = std::min(width, x1);
  const int iy1 = std::min(height, y1);
  if (ix0 >= ix1 || iy0 >= iy1) {
    return;
  }
  for (int y = iy0; y < iy1; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
    for (int x = ix0; x < ix1; ++x) {
      put_replace(row + static_cast<std::size_t>(x) * 4, color);
    }
  }
  grow(dirty, ix0, iy0, ix1, iy1, width, height);
}

void stamp_round(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                 int size, Color color, bool antialias, Rect* dirty) {
  if (rgba == nullptr || size < 1) {
    return;
  }
  if (size == 1 && !antialias) {
    stamp_square(rgba, width, height, stride, static_cast<int>(std::floor(cx)),
                 static_cast<int>(std::floor(cy)), 1, color, dirty);
    return;
  }

  const double radius = static_cast<double>(size) * 0.5;
  const double pad = antialias ? 1.0 : 0.0;
  const int x0 = static_cast<int>(std::floor(cx - radius - pad));
  const int y0 = static_cast<int>(std::floor(cy - radius - pad));
  const int x1 = static_cast<int>(std::ceil(cx + radius + pad));
  const int y1 = static_cast<int>(std::ceil(cy + radius + pad));
  const int ix0 = std::max(0, x0);
  const int iy0 = std::max(0, y0);
  const int ix1 = std::min(width, x1);
  const int iy1 = std::min(height, y1);
  if (ix0 >= ix1 || iy0 >= iy1) {
    return;
  }

  const double r2 = radius * radius;
  for (int y = iy0; y < iy1; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
    for (int x = ix0; x < ix1; ++x) {
      const double dx = (static_cast<double>(x) + 0.5) - cx;
      const double dy = (static_cast<double>(y) + 0.5) - cy;
      const double d2 = dx * dx + dy * dy;
      if (!antialias) {
        if (d2 <= r2) {
          put_replace(row + static_cast<std::size_t>(x) * 4, color);
        }
        continue;
      }
      const double dist = std::sqrt(d2);
      const double coverage = radius + 0.5 - dist;
      if (coverage <= 0.0) {
        continue;
      }
      int cov = 255;
      if (coverage < 1.0) {
        cov = static_cast<int>(coverage * 255.0 + 0.5);
        if (cov < 1) {
          continue;
        }
        if (cov > 255) {
          cov = 255;
        }
      }
      put_blend(row + static_cast<std::size_t>(x) * 4, color, cov);
    }
  }
  grow(dirty, ix0, iy0, ix1, iy1, width, height);
}

void stroke_pencil(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                   int y1, int size, Color color, Rect* dirty) {
  int dx = std::abs(x1 - x0);
  int dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int x = x0;
  int y = y0;
  for (;;) {
    stamp_square(rgba, width, height, stride, x, y, size, color, dirty);
    if (x == x1 && y == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

void stroke_brush(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                  double x1, double y1, int size, Color color, bool antialias, Rect* dirty) {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001) {
    stamp_round(rgba, width, height, stride, x0, y0, size, color, antialias, dirty);
    return;
  }
  const double step = 1.0;
  const int n = static_cast<int>(std::ceil(len / step));
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n);
    stamp_round(rgba, width, height, stride, x0 + dx * t, y0 + dy * t, size, color, antialias,
                dirty);
  }
}

void color_erase_stamp(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                       int size, Color target, Color replacement, int tolerance, Rect* dirty) {
  if (rgba == nullptr || size < 1) {
    return;
  }
  if (tolerance < 0) {
    tolerance = 0;
  }
  const double radius = static_cast<double>(size) * 0.5;
  const int x0 = static_cast<int>(std::floor(cx - radius));
  const int y0 = static_cast<int>(std::floor(cy - radius));
  const int x1 = static_cast<int>(std::ceil(cx + radius));
  const int y1 = static_cast<int>(std::ceil(cy + radius));
  const int ix0 = std::max(0, x0);
  const int iy0 = std::max(0, y0);
  const int ix1 = std::min(width, x1);
  const int iy1 = std::min(height, y1);
  if (ix0 >= ix1 || iy0 >= iy1) {
    return;
  }
  const double r2 = radius * radius;
  bool any = false;
  for (int y = iy0; y < iy1; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
    for (int x = ix0; x < ix1; ++x) {
      const double dx = (static_cast<double>(x) + 0.5) - cx;
      const double dy = (static_cast<double>(y) + 0.5) - cy;
      if (dx * dx + dy * dy > r2) {
        continue;
      }
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      const Color cur{p[0], p[1], p[2], p[3]};
      if (color_chebyshev(cur, target) > tolerance) {
        continue;
      }
      put_replace(p, replacement);
      any = true;
    }
  }
  if (any) {
    grow(dirty, ix0, iy0, ix1, iy1, width, height);
  }
}

void color_erase_stroke(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                        double x1, double y1, int size, Color target, Color replacement,
                        int tolerance, Rect* dirty) {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001) {
    color_erase_stamp(rgba, width, height, stride, x0, y0, size, target, replacement, tolerance,
                      dirty);
    return;
  }
  const int n = static_cast<int>(std::ceil(len));
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n);
    color_erase_stamp(rgba, width, height, stride, x0 + dx * t, y0 + dy * t, size, target,
                      replacement, tolerance, dirty);
  }
}

void spray_dots(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                int radius, int density, Color color, std::uint32_t* rng, Rect* dirty) {
  if (rgba == nullptr || rng == nullptr || radius < 1) {
    return;
  }
  if (density < 1) {
    density = 1;
  }
  if (density > 100) {
    density = 100;
  }
  // density 100 ≈ one dot per ~4 pixels of the disk; density 1 is a few specks.
  const int area = std::max(1, radius * radius);
  int count = std::max(1, (density * area) / 80);
  const double r = static_cast<double>(radius);
  int minx = width;
  int miny = height;
  int maxx = -1;
  int maxy = -1;
  auto next_u = [&]() {
    *rng = *rng * 1664525u + 1013904223u;
    return *rng;
  };
  for (int i = 0; i < count; ++i) {
    // Rejection sample in the disk.
    double dx = 0;
    double dy = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
      const double u = static_cast<double>(next_u() & 0xFFFFu) / 65535.0;
      const double v = static_cast<double>(next_u() & 0xFFFFu) / 65535.0;
      dx = (u * 2.0 - 1.0) * r;
      dy = (v * 2.0 - 1.0) * r;
      if (dx * dx + dy * dy <= r * r) {
        break;
      }
    }
    const int x = static_cast<int>(std::floor(cx + dx));
    const int y = static_cast<int>(std::floor(cy + dy));
    if (x < 0 || y < 0 || x >= width || y >= height) {
      continue;
    }
    put_replace(rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4, color);
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
  if (dirty != nullptr && maxx >= minx) {
    *dirty = rect_union(*dirty, Rect{minx, miny, maxx - minx + 1, maxy - miny + 1});
  }
}

void stroke_spray(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                  double x1, double y1, int radius, int density, Color color, std::uint32_t* rng,
                  Rect* dirty) {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len = std::sqrt(dx * dx + dy * dy);
  const double step = std::max(1.0, static_cast<double>(std::max(1, radius)) / 4.0);
  if (len < 0.001) {
    spray_dots(rgba, width, height, stride, x0, y0, radius, density, color, rng, dirty);
    return;
  }
  const int n = std::max(1, static_cast<int>(std::ceil(len / step)));
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n);
    spray_dots(rgba, width, height, stride, x0 + dx * t, y0 + dy * t, radius, density, color, rng,
               dirty);
  }
}

}  // namespace lundukepaint
