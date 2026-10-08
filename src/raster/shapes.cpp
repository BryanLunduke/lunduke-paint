// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/shapes.hpp"

#include "raster/pattern.hpp"
#include "raster/stroke.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace lundukepaint {
namespace {

void put_color(std::uint8_t* p, Color color) {
  p[0] = color.r;
  p[1] = color.g;
  p[2] = color.b;
  p[3] = color.a;
}

Color fill_color_at(int x, int y, Color solid, const Pattern* pattern, Color pattern_bg) {
  if (pattern == nullptr) {
    return solid;
  }
  return pattern->color_at(x, y, solid, pattern_bg);
}

void fill_rect_color(std::uint8_t* rgba, int width, int height, int stride, Rect rect, Color color,
                     Rect* dirty, const Pattern* pattern, Color pattern_bg) {
  rect = rect_intersect(rect, Rect{0, 0, width, height});
  if (rect.empty()) {
    return;
  }
  for (int y = rect.y; y < rect.y2(); ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
    for (int x = rect.x; x < rect.x2(); ++x) {
      put_color(row + static_cast<std::size_t>(x) * 4,
                fill_color_at(x, y, color, pattern, pattern_bg));
    }
  }
  if (dirty != nullptr) {
    *dirty = rect_union(*dirty, rect);
  }
}

}  // namespace

void constrain_line_45(int x0, int y0, int* x1, int* y1) {
  if (x1 == nullptr || y1 == nullptr) {
    return;
  }
  const int dx = *x1 - x0;
  const int dy = *y1 - y0;
  const int adx = std::abs(dx);
  const int ady = std::abs(dy);
  if (adx == 0 && ady == 0) {
    return;
  }
  if (adx == 0 || (ady > 0 && ady * 2 < adx)) {
    *y1 = y0;
    return;
  }
  if (ady == 0 || (adx > 0 && adx * 2 < ady)) {
    *x1 = x0;
    return;
  }
  const int m = std::max(adx, ady);
  *x1 = x0 + (dx < 0 ? -m : m);
  *y1 = y0 + (dy < 0 ? -m : m);
}

void constrain_square(int x0, int y0, int* x1, int* y1) {
  if (x1 == nullptr || y1 == nullptr) {
    return;
  }
  const int adx = std::abs(*x1 - x0);
  const int ady = std::abs(*y1 - y0);
  const int m = std::max(adx, ady);
  *x1 = x0 + (*x1 < x0 ? -m : m);
  *y1 = y0 + (*y1 < y0 ? -m : m);
}

void draw_line(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
               int y1, int thickness, Color color, bool antialias, Rect* dirty) {
  if (thickness < 1) {
    thickness = 1;
  }
  if (antialias) {
    stroke_brush(rgba, width, height, stride, static_cast<double>(x0) + 0.5,
                 static_cast<double>(y0) + 0.5, static_cast<double>(x1) + 0.5,
                 static_cast<double>(y1) + 0.5, thickness, color, true, dirty);
  } else {
    stroke_pencil(rgba, width, height, stride, x0, y0, x1, y1, thickness, color, dirty);
  }
}

void draw_rectangle(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                    int y1, int thickness, Color color, ShapeFillMode mode, bool antialias,
                    Rect* dirty, const Pattern* pattern, Color pattern_bg) {
  if (thickness < 1) {
    thickness = 1;
  }
  const int left = std::min(x0, x1);
  const int top = std::min(y0, y1);
  const int right = std::max(x0, x1);
  const int bottom = std::max(y0, y1);
  if (mode == ShapeFillMode::Fill || mode == ShapeFillMode::Both) {
    fill_rect_color(rgba, width, height, stride, Rect{left, top, right - left + 1, bottom - top + 1},
                    color, dirty, pattern, pattern_bg);
  }
  if (mode == ShapeFillMode::Stroke || mode == ShapeFillMode::Both) {
    draw_line(rgba, width, height, stride, left, top, right, top, thickness, color, antialias,
              dirty);
    draw_line(rgba, width, height, stride, right, top, right, bottom, thickness, color, antialias,
              dirty);
    draw_line(rgba, width, height, stride, right, bottom, left, bottom, thickness, color, antialias,
              dirty);
    draw_line(rgba, width, height, stride, left, bottom, left, top, thickness, color, antialias,
              dirty);
  }
}

void draw_ellipse(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                  int y1, int thickness, Color color, ShapeFillMode mode, bool antialias,
                  Rect* dirty, const Pattern* pattern, Color pattern_bg) {
  if (rgba == nullptr) {
    return;
  }
  if (thickness < 1) {
    thickness = 1;
  }
  const int left = std::min(x0, x1);
  const int top = std::min(y0, y1);
  const int right = std::max(x0, x1);
  const int bottom = std::max(y0, y1);
  const double cx = (static_cast<double>(left) + static_cast<double>(right)) * 0.5 + 0.5;
  const double cy = (static_cast<double>(top) + static_cast<double>(bottom)) * 0.5 + 0.5;
  const double rx = std::max(0.5, (static_cast<double>(right - left) + 1.0) * 0.5);
  const double ry = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) * 0.5);
  const int pad = thickness + 1;
  const int ix0 = std::max(0, left - pad);
  const int iy0 = std::max(0, top - pad);
  const int ix1 = std::min(width, right + pad + 1);
  const int iy1 = std::min(height, bottom + pad + 1);
  if (ix0 >= ix1 || iy0 >= iy1) {
    return;
  }

  const double inv_rx = 1.0 / rx;
  const double inv_ry = 1.0 / ry;
  const double thick = static_cast<double>(thickness);
  const bool do_fill = (mode == ShapeFillMode::Fill || mode == ShapeFillMode::Both);
  const bool do_stroke = (mode == ShapeFillMode::Stroke || mode == ShapeFillMode::Both);

  for (int y = iy0; y < iy1; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
    const double dy = (static_cast<double>(y) + 0.5) - cy;
    for (int x = ix0; x < ix1; ++x) {
      const double dx = (static_cast<double>(x) + 0.5) - cx;
      const double nx = dx * inv_rx;
      const double ny = dy * inv_ry;
      const double rnorm = std::sqrt(nx * nx + ny * ny);
      const double along = std::hypot(dx / rx, dy / ry);
      const double dist_px = std::abs(along - 1.0) * std::min(rx, ry);
      const bool on_fill = do_fill && rnorm <= 1.0;
      const bool on_stroke = do_stroke && dist_px <= thick * 0.5 + (antialias ? 0.5 : 0.0);
      if (!on_fill && !on_stroke) {
        continue;
      }
      Color paint = color;
      if (on_fill && !on_stroke) {
        paint = fill_color_at(x, y, color, pattern, pattern_bg);
      }
      put_color(row + static_cast<std::size_t>(x) * 4, paint);
    }
  }
  if (dirty != nullptr) {
    *dirty = rect_union(*dirty, Rect{ix0, iy0, ix1 - ix0, iy1 - iy0});
  }
}

void draw_rounded_rect(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0,
                       int x1, int y1, int thickness, int radius, Color color, ShapeFillMode mode,
                       bool antialias, Rect* dirty, const Pattern* pattern, Color pattern_bg) {
  if (rgba == nullptr) {
    return;
  }
  if (thickness < 1) {
    thickness = 1;
  }
  if (radius < 0) {
    radius = 0;
  }
  const int left = std::min(x0, x1);
  const int top = std::min(y0, y1);
  const int right = std::max(x0, x1);
  const int bottom = std::max(y0, y1);
  const double hw = std::max(0.5, (static_cast<double>(right - left) + 1.0) * 0.5);
  const double hh = std::max(0.5, (static_cast<double>(bottom - top) + 1.0) * 0.5);
  const double cx = (static_cast<double>(left) + static_cast<double>(right)) * 0.5 + 0.5;
  const double cy = (static_cast<double>(top) + static_cast<double>(bottom)) * 0.5 + 0.5;
  double r = std::min(static_cast<double>(radius), std::min(hw, hh));
  const int pad = thickness + 1;
  const int ix0 = std::max(0, left - pad);
  const int iy0 = std::max(0, top - pad);
  const int ix1 = std::min(width, right + pad + 1);
  const int iy1 = std::min(height, bottom + pad + 1);
  if (ix0 >= ix1 || iy0 >= iy1) {
    return;
  }
  const double thick = static_cast<double>(thickness) * 0.5;
  const bool do_fill = (mode == ShapeFillMode::Fill || mode == ShapeFillMode::Both);
  const bool do_stroke = (mode == ShapeFillMode::Stroke || mode == ShapeFillMode::Both);
  const double aa = antialias ? 0.5 : 0.0;
  for (int y = iy0; y < iy1; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
    const double py = (static_cast<double>(y) + 0.5) - cy;
    for (int x = ix0; x < ix1; ++x) {
      const double px = (static_cast<double>(x) + 0.5) - cx;
      const double dx = std::abs(px) - (hw - r);
      const double dy = std::abs(py) - (hh - r);
      const double ox = std::max(dx, 0.0);
      const double oy = std::max(dy, 0.0);
      const double sdf = std::sqrt(ox * ox + oy * oy) + std::min(std::max(dx, dy), 0.0) - r;
      const bool on_fill = do_fill && sdf <= 0.0;
      const bool on_stroke = do_stroke && std::abs(sdf) <= thick + aa;
      if (!on_fill && !on_stroke) {
        continue;
      }
      Color paint = color;
      if (on_fill && !on_stroke) {
        paint = fill_color_at(x, y, color, pattern, pattern_bg);
      }
      put_color(row + static_cast<std::size_t>(x) * 4, paint);
    }
  }
  if (dirty != nullptr) {
    *dirty = rect_union(*dirty, Rect{ix0, iy0, ix1 - ix0, iy1 - iy0});
  }
}

void draw_polyline(std::uint8_t* rgba, int width, int height, int stride, const int* xs,
                   const int* ys, int count, int thickness, Color color, bool antialias,
                   Rect* dirty) {
  if (rgba == nullptr || xs == nullptr || ys == nullptr || count < 1) {
    return;
  }
  if (count == 1) {
    draw_line(rgba, width, height, stride, xs[0], ys[0], xs[0], ys[0], thickness, color, antialias,
              dirty);
    return;
  }
  for (int i = 1; i < count; ++i) {
    draw_line(rgba, width, height, stride, xs[i - 1], ys[i - 1], xs[i], ys[i], thickness, color,
              antialias, dirty);
  }
}

void fill_polygon_mask(std::uint8_t* mask, int width, int height, const int* xs, const int* ys,
                       int count) {
  if (mask == nullptr || xs == nullptr || ys == nullptr || count < 3 || width < 1 || height < 1) {
    return;
  }
  int miny = height;
  int maxy = -1;
  for (int i = 0; i < count; ++i) {
    if (ys[i] < miny) {
      miny = ys[i];
    }
    if (ys[i] > maxy) {
      maxy = ys[i];
    }
  }
  miny = std::max(0, miny);
  maxy = std::min(height - 1, maxy);
  std::vector<int> xs_hit;
  xs_hit.reserve(static_cast<std::size_t>(count));
  for (int y = miny; y <= maxy; ++y) {
    xs_hit.clear();
    const double scan = static_cast<double>(y) + 0.5;
    for (int i = 0; i < count; ++i) {
      const int j = (i + 1) % count;
      double y0 = static_cast<double>(ys[i]);
      double y1 = static_cast<double>(ys[j]);
      double x0 = static_cast<double>(xs[i]);
      double x1 = static_cast<double>(xs[j]);
      if (y0 == y1) {
        continue;
      }
      if (y0 > y1) {
        std::swap(y0, y1);
        std::swap(x0, x1);
      }
      if (scan < y0 || scan >= y1) {
        continue;
      }
      const double t = (scan - y0) / (y1 - y0);
      xs_hit.push_back(static_cast<int>(std::floor(x0 + t * (x1 - x0))));
    }
    std::sort(xs_hit.begin(), xs_hit.end());
    for (std::size_t k = 0; k + 1 < xs_hit.size(); k += 2) {
      int a = std::max(0, xs_hit[k]);
      int b = std::min(width - 1, xs_hit[k + 1]);
      for (int x = a; x <= b; ++x) {
        mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)] = 255;
      }
    }
  }
}

void draw_polygon(std::uint8_t* rgba, int width, int height, int stride, const int* xs,
                  const int* ys, int count, int thickness, Color color, ShapeFillMode mode,
                  bool antialias, Rect* dirty, const Pattern* pattern, Color pattern_bg) {
  if (rgba == nullptr || xs == nullptr || ys == nullptr || count < 1) {
    return;
  }
  if (mode == ShapeFillMode::Fill || mode == ShapeFillMode::Both) {
    if (count >= 3) {
      std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * static_cast<std::size_t>(height),
                                     0);
      fill_polygon_mask(mask.data(), width, height, xs, ys, count);
      int minx = width;
      int miny = height;
      int maxx = -1;
      int maxy = -1;
      for (int y = 0; y < height; ++y) {
        std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
        for (int x = 0; x < width; ++x) {
          if (mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] == 0) {
            continue;
          }
          put_color(row + static_cast<std::size_t>(x) * 4,
                    fill_color_at(x, y, color, pattern, pattern_bg));
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
        *dirty = rect_union(*dirty, Rect{minx, miny, maxx - minx + 1, maxy - miny + 1});
      }
    }
  }
  if (mode == ShapeFillMode::Stroke || mode == ShapeFillMode::Both) {
    draw_polyline(rgba, width, height, stride, xs, ys, count, thickness, color, antialias, dirty);
    if (count >= 2) {
      draw_line(rgba, width, height, stride, xs[count - 1], ys[count - 1], xs[0], ys[0], thickness,
                color, antialias, dirty);
    }
  }
}

void draw_cubic_bezier(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0,
                       int x1, int y1, int x2, int y2, int x3, int y3, int thickness, Color color,
                       bool antialias, Rect* dirty) {
  if (rgba == nullptr) {
    return;
  }
  const double dx = static_cast<double>(x3 - x0);
  const double dy = static_cast<double>(y3 - y0);
  const double cdx = static_cast<double>(x1 - x0) + static_cast<double>(x2 - x3);
  const double cdy = static_cast<double>(y1 - y0) + static_cast<double>(y2 - y3);
  int steps = static_cast<int>(std::ceil(std::hypot(dx, dy) + std::hypot(cdx, cdy)));
  if (steps < 8) {
    steps = 8;
  }
  if (steps > 1024) {
    steps = 1024;
  }
  int px = x0;
  int py = y0;
  for (int i = 1; i <= steps; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(steps);
    const double u = 1.0 - t;
    const double a = u * u * u;
    const double b = 3.0 * u * u * t;
    const double c = 3.0 * u * t * t;
    const double d = t * t * t;
    const int x = static_cast<int>(std::floor(a * x0 + b * x1 + c * x2 + d * x3 + 0.5));
    const int y = static_cast<int>(std::floor(a * y0 + b * y1 + c * y2 + d * y3 + 0.5));
    draw_line(rgba, width, height, stride, px, py, x, y, thickness, color, antialias, dirty);
    px = x;
    py = y;
  }
}

void fill_ellipse_mask(std::uint8_t* mask, int width, int height) {
  if (mask == nullptr || width < 1 || height < 1) {
    return;
  }
  const double cx = (static_cast<double>(width)) * 0.5;
  const double cy = (static_cast<double>(height)) * 0.5;
  const double rx = std::max(0.5, static_cast<double>(width) * 0.5);
  const double ry = std::max(0.5, static_cast<double>(height) * 0.5);
  const double inv_rx = 1.0 / rx;
  const double inv_ry = 1.0 / ry;
  for (int y = 0; y < height; ++y) {
    const double ny = ((static_cast<double>(y) + 0.5) - cy) * inv_ry;
    for (int x = 0; x < width; ++x) {
      const double nx = ((static_cast<double>(x) + 0.5) - cx) * inv_rx;
      if (nx * nx + ny * ny <= 1.0) {
        mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
             static_cast<std::size_t>(x)] = 255;
      }
    }
  }
}

void stroke_polyline_mask(std::uint8_t* mask, int width, int height, const int* xs, const int* ys,
                          int count, bool closed) {
  if (mask == nullptr || xs == nullptr || ys == nullptr || count < 1) {
    return;
  }
  auto plot = [&](int x, int y) {
    if (x < 0 || y < 0 || x >= width || y >= height) {
      return;
    }
    mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
        255;
  };
  auto line = [&](int x0, int y0, int x1, int y1) {
    int dx = std::abs(x1 - x0);
    int dy = -std::abs(y1 - y0);
    const int sx = x0 < x1 ? 1 : -1;
    const int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int x = x0;
    int y = y0;
    for (;;) {
      plot(x, y);
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
  };
  if (count == 1) {
    plot(xs[0], ys[0]);
    return;
  }
  for (int i = 1; i < count; ++i) {
    line(xs[i - 1], ys[i - 1], xs[i], ys[i]);
  }
  if (closed && count >= 2) {
    line(xs[count - 1], ys[count - 1], xs[0], ys[0]);
  }
}

}  // namespace lundukepaint
