// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/transform.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lundukepaint {
namespace {

Color get(const std::uint8_t* rgba, int stride, int x, int y) {
  const std::uint8_t* p =
      rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

void put(std::uint8_t* rgba, int stride, int x, int y, Color c) {
  std::uint8_t* p = rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

void put_rgba(std::uint8_t* p, Color c) {
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

}  // namespace

void flip_h(std::uint8_t* rgba, int width, int height, int stride) {
  if (rgba == nullptr || width < 2 || height < 1) {
    return;
  }
  std::uint8_t tmp[4];
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * stride;
    for (int x = 0; x < width / 2; ++x) {
      std::uint8_t* a = row + static_cast<std::size_t>(x) * 4;
      std::uint8_t* b = row + static_cast<std::size_t>(width - 1 - x) * 4;
      std::memcpy(tmp, a, 4);
      std::memcpy(a, b, 4);
      std::memcpy(b, tmp, 4);
    }
  }
}

void flip_v(std::uint8_t* rgba, int width, int height, int stride) {
  if (rgba == nullptr || width < 1 || height < 2) {
    return;
  }
  std::vector<std::uint8_t> tmp(static_cast<std::size_t>(width) * 4);
  for (int y = 0; y < height / 2; ++y) {
    std::uint8_t* a = rgba + static_cast<std::size_t>(y) * stride;
    std::uint8_t* b = rgba + static_cast<std::size_t>(height - 1 - y) * stride;
    const std::size_t n = static_cast<std::size_t>(width) * 4;
    std::memcpy(tmp.data(), a, n);
    std::memcpy(a, b, n);
    std::memcpy(b, tmp.data(), n);
  }
}

void rotate_180(std::uint8_t* rgba, int width, int height, int stride) {
  flip_h(rgba, width, height, stride);
  flip_v(rgba, width, height, stride);
}

void rotate_90_cw(const std::uint8_t* src, int src_w, int src_h, int src_stride, std::uint8_t* dest,
                  int dest_stride) {
  if (src == nullptr || dest == nullptr) {
    return;
  }
  for (int y = 0; y < src_w; ++y) {
    for (int x = 0; x < src_h; ++x) {
      put(dest, dest_stride, x, y, get(src, src_stride, y, src_h - 1 - x));
    }
  }
}

void rotate_90_ccw(const std::uint8_t* src, int src_w, int src_h, int src_stride, std::uint8_t* dest,
                   int dest_stride) {
  if (src == nullptr || dest == nullptr) {
    return;
  }
  for (int y = 0; y < src_w; ++y) {
    for (int x = 0; x < src_h; ++x) {
      put(dest, dest_stride, x, y, get(src, src_stride, src_w - 1 - y, x));
    }
  }
}

void scale_nearest(const std::uint8_t* src, int src_w, int src_h, int src_stride, std::uint8_t* dest,
                   int dest_w, int dest_h, int dest_stride) {
  if (src == nullptr || dest == nullptr || src_w < 1 || src_h < 1 || dest_w < 1 || dest_h < 1) {
    return;
  }
  for (int y = 0; y < dest_h; ++y) {
    const int sy = std::min(src_h - 1, y * src_h / dest_h);
    std::uint8_t* drow = dest + static_cast<std::size_t>(y) * dest_stride;
    for (int x = 0; x < dest_w; ++x) {
      const int sx = std::min(src_w - 1, x * src_w / dest_w);
      const Color c = get(src, src_stride, sx, sy);
      put_rgba(drow + static_cast<std::size_t>(x) * 4, c);
    }
  }
}

void scale_bilinear(const std::uint8_t* src, int src_w, int src_h, int src_stride, std::uint8_t* dest,
                    int dest_w, int dest_h, int dest_stride) {
  if (src == nullptr || dest == nullptr || src_w < 1 || src_h < 1 || dest_w < 1 || dest_h < 1) {
    return;
  }
  for (int y = 0; y < dest_h; ++y) {
    const double fy = (static_cast<double>(y) + 0.5) * src_h / dest_h - 0.5;
    const int y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, src_h - 1);
    const int y1 = std::min(src_h - 1, y0 + 1);
    const double ty = fy - static_cast<double>(y0);
    std::uint8_t* drow = dest + static_cast<std::size_t>(y) * dest_stride;
    for (int x = 0; x < dest_w; ++x) {
      const double fx = (static_cast<double>(x) + 0.5) * src_w / dest_w - 0.5;
      const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, src_w - 1);
      const int x1 = std::min(src_w - 1, x0 + 1);
      const double tx = fx - static_cast<double>(x0);
      const Color c00 = get(src, src_stride, x0, y0);
      const Color c10 = get(src, src_stride, x1, y0);
      const Color c01 = get(src, src_stride, x0, y1);
      const Color c11 = get(src, src_stride, x1, y1);
      auto mix = [&](int ch00, int ch10, int ch01, int ch11) {
        const double a = ch00 * (1.0 - tx) + ch10 * tx;
        const double b = ch01 * (1.0 - tx) + ch11 * tx;
        const int v = static_cast<int>(a * (1.0 - ty) + b * ty + 0.5);
        return static_cast<std::uint8_t>(std::clamp(v, 0, 255));
      };
      Color out;
      out.r = mix(c00.r, c10.r, c01.r, c11.r);
      out.g = mix(c00.g, c10.g, c01.g, c11.g);
      out.b = mix(c00.b, c10.b, c01.b, c11.b);
      out.a = mix(c00.a, c10.a, c01.a, c11.a);
      put_rgba(drow + static_cast<std::size_t>(x) * 4, out);
    }
  }
}

void resize_canvas(const std::uint8_t* src, int src_w, int src_h, int src_stride, std::uint8_t* dest,
                   int dest_w, int dest_h, int dest_stride, Color fill) {
  if (dest == nullptr || dest_w < 1 || dest_h < 1) {
    return;
  }
  for (int y = 0; y < dest_h; ++y) {
    std::uint8_t* drow = dest + static_cast<std::size_t>(y) * dest_stride;
    for (int x = 0; x < dest_w; ++x) {
      if (src != nullptr && x < src_w && y < src_h) {
        put_rgba(drow + static_cast<std::size_t>(x) * 4, get(src, src_stride, x, y));
      } else {
        put_rgba(drow + static_cast<std::size_t>(x) * 4, fill);
      }
    }
  }
}

void crop_rect(const std::uint8_t* src, int src_w, int src_h, int src_stride, Rect rect,
               std::uint8_t* dest, int dest_stride) {
  rect = rect_intersect(rect, Rect{0, 0, src_w, src_h});
  if (src == nullptr || dest == nullptr || rect.empty()) {
    return;
  }
  for (int y = 0; y < rect.h; ++y) {
    const std::uint8_t* s =
        src + static_cast<std::size_t>(rect.y + y) * src_stride + static_cast<std::size_t>(rect.x) * 4;
    std::uint8_t* d = dest + static_cast<std::size_t>(y) * dest_stride;
    std::memcpy(d, s, static_cast<std::size_t>(rect.w) * 4);
  }
}

Rect autocrop_bounds(const std::uint8_t* rgba, int width, int height, int stride) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return {};
  }
  // A solid layer has no border to trim. Stopping the scan one row short of
  // the edge used to report the last pixel as content and shrink the document.
  const Color solid = get(rgba, stride, 0, 0);
  bool uniform = true;
  for (int y = 0; y < height && uniform; ++y) {
    for (int x = 0; x < width; ++x) {
      if (get(rgba, stride, x, y) != solid) {
        uniform = false;
        break;
      }
    }
  }
  if (uniform) {
    return {};
  }
  auto row_uniform = [&](int y, Color c) {
    for (int x = 0; x < width; ++x) {
      if (get(rgba, stride, x, y) != c) {
        return false;
      }
    }
    return true;
  };
  int top = 0;
  while (top < height - 1 && row_uniform(top, get(rgba, stride, 0, top))) {
    ++top;
  }
  int bottom = height - 1;
  while (bottom > top && row_uniform(bottom, get(rgba, stride, 0, bottom))) {
    --bottom;
  }
  // Left/right run only over the rows still inside the crop. A uniform top
  // border of a different color must not keep a uniform side margin.
  auto col_uniform = [&](int x, Color c) {
    for (int y = top; y <= bottom; ++y) {
      if (get(rgba, stride, x, y) != c) {
        return false;
      }
    }
    return true;
  };
  int left = 0;
  while (left < width - 1 && col_uniform(left, get(rgba, stride, left, top))) {
    ++left;
  }
  int right = width - 1;
  while (right > left && col_uniform(right, get(rgba, stride, right, top))) {
    --right;
  }
  return {left, top, right - left + 1, bottom - top + 1};
}

namespace {

std::vector<std::uint8_t> copy_tight(const std::uint8_t* src, int w, int h, int stride) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(std::max(1, w)) *
                                    static_cast<std::size_t>(std::max(1, h)) * 4,
                                0);
  if (src == nullptr || w < 1 || h < 1) {
    return out;
  }
  const int row = w * 4;
  for (int y = 0; y < h; ++y) {
    std::memcpy(out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(row),
                src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                static_cast<std::size_t>(row));
  }
  return out;
}

int map_edge(int v, int from, int to) {
  if (from == 0) {
    return 0;
  }
  return static_cast<int>(std::llround(static_cast<long double>(v) * static_cast<long double>(to) /
                                       static_cast<long double>(from)));
}

}  // namespace

PlacedPixels place_flip_h(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                          int oy, int canvas_w, int canvas_h) {
  (void)canvas_h;
  PlacedPixels out;
  out.width = std::max(1, src_w);
  out.height = std::max(1, src_h);
  out.offset_x = canvas_w - ox - src_w;
  out.offset_y = oy;
  out.pixels = copy_tight(src, src_w, src_h, src_stride);
  flip_h(out.pixels.data(), out.width, out.height, out.width * 4);
  return out;
}

PlacedPixels place_flip_v(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                          int oy, int canvas_w, int canvas_h) {
  (void)canvas_w;
  PlacedPixels out;
  out.width = std::max(1, src_w);
  out.height = std::max(1, src_h);
  out.offset_x = ox;
  out.offset_y = canvas_h - oy - src_h;
  out.pixels = copy_tight(src, src_w, src_h, src_stride);
  flip_v(out.pixels.data(), out.width, out.height, out.width * 4);
  return out;
}

PlacedPixels place_rotate_180(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                              int oy, int canvas_w, int canvas_h) {
  PlacedPixels out;
  out.width = std::max(1, src_w);
  out.height = std::max(1, src_h);
  out.offset_x = canvas_w - ox - src_w;
  out.offset_y = canvas_h - oy - src_h;
  out.pixels = copy_tight(src, src_w, src_h, src_stride);
  rotate_180(out.pixels.data(), out.width, out.height, out.width * 4);
  return out;
}

PlacedPixels place_rotate_90_cw(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                                int ox, int oy, int canvas_w, int canvas_h) {
  (void)canvas_w;
  PlacedPixels out;
  out.width = std::max(1, src_h);
  out.height = std::max(1, src_w);
  out.offset_x = canvas_h - oy - src_h;
  out.offset_y = ox;
  out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
  if (src != nullptr && src_w > 0 && src_h > 0) {
    rotate_90_cw(src, src_w, src_h, src_stride, out.pixels.data(), out.width * 4);
  }
  return out;
}

PlacedPixels place_rotate_90_ccw(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                                 int ox, int oy, int canvas_w, int canvas_h) {
  (void)canvas_h;
  PlacedPixels out;
  out.width = std::max(1, src_h);
  out.height = std::max(1, src_w);
  out.offset_x = oy;
  out.offset_y = canvas_w - ox - src_w;
  out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
  if (src != nullptr && src_w > 0 && src_h > 0) {
    rotate_90_ccw(src, src_w, src_h, src_stride, out.pixels.data(), out.width * 4);
  }
  return out;
}

PlacedPixels place_scale(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                         int oy, int canvas_w, int canvas_h, int new_w, int new_h, bool nearest) {
  int nw = new_w;
  int nh = new_h;
  int nox = 0;
  int noy = 0;
  if (!(ox == 0 && oy == 0 && src_w == canvas_w && src_h == canvas_h)) {
    nox = map_edge(ox, canvas_w, new_w);
    noy = map_edge(oy, canvas_h, new_h);
    nw = std::max(1, map_edge(ox + src_w, canvas_w, new_w) - nox);
    nh = std::max(1, map_edge(oy + src_h, canvas_h, new_h) - noy);
  }
  PlacedPixels out;
  out.width = std::max(1, nw);
  out.height = std::max(1, nh);
  out.offset_x = nox;
  out.offset_y = noy;
  out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
  if (nearest) {
    scale_nearest(src, src_w, src_h, src_stride, out.pixels.data(), out.width, out.height,
                  out.width * 4);
  } else {
    scale_bilinear(src, src_w, src_h, src_stride, out.pixels.data(), out.width, out.height,
                   out.width * 4);
  }
  return out;
}

PlacedPixels place_crop(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox, int oy,
                        Rect crop) {
  const Rect layer_rect{ox, oy, src_w, src_h};
  const Rect vis = rect_intersect(layer_rect, crop);
  PlacedPixels out;
  if (vis.empty() || (vis.x == ox && vis.y == oy && vis.w == src_w && vis.h == src_h)) {
    out.width = std::max(1, src_w);
    out.height = std::max(1, src_h);
    out.offset_x = ox - crop.x;
    out.offset_y = oy - crop.y;
    out.pixels = copy_tight(src, src_w, src_h, src_stride);
    return out;
  }
  const Rect local{vis.x - ox, vis.y - oy, vis.w, vis.h};
  out.width = local.w;
  out.height = local.h;
  out.offset_x = vis.x - crop.x;
  out.offset_y = vis.y - crop.y;
  out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
  crop_rect(src, src_w, src_h, src_stride, local, out.pixels.data(), out.width * 4);
  return out;
}

PlacedPixels place_resize_canvas(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                                 int ox, int oy, int canvas_w, int canvas_h, int new_w, int new_h,
                                 Color fill) {
  PlacedPixels out;
  if (ox == 0 && oy == 0 && src_w == canvas_w && src_h == canvas_h) {
    out.width = std::max(1, new_w);
    out.height = std::max(1, new_h);
    out.offset_x = 0;
    out.offset_y = 0;
    out.pixels.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4,
                      0);
    resize_canvas(src, src_w, src_h, src_stride, out.pixels.data(), out.width, out.height,
                  out.width * 4, fill);
    return out;
  }
  out.width = std::max(1, src_w);
  out.height = std::max(1, src_h);
  out.offset_x = ox;
  out.offset_y = oy;
  out.pixels = copy_tight(src, src_w, src_h, src_stride);
  return out;
}

namespace {

std::uint8_t coverage_at(const std::uint8_t* coverage, int w, int x, int y) {
  if (coverage == nullptr) {
    return 1;
  }
  return coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] ==
                 0
             ? 0
             : 1;
}

}  // namespace

SampledPixels rotate_nearest_angle(const std::uint8_t* src, int width, int height, int stride,
                                   const std::uint8_t* coverage, double degrees_clockwise) {
  SampledPixels out;
  if (src == nullptr || width < 1 || height < 1) {
    return out;
  }
  const double rad = degrees_clockwise * 3.14159265358979323846 / 180.0;
  const double c = std::cos(rad);
  const double s = std::sin(rad);
  const double cx = (width - 1) * 0.5;
  const double cy = (height - 1) * 0.5;
  const double corners[4][2] = {{0, 0}, {static_cast<double>(width - 1), 0},
                                {0, static_cast<double>(height - 1)},
                                {static_cast<double>(width - 1), static_cast<double>(height - 1)}};
  double min_x = 1e9;
  double min_y = 1e9;
  double max_x = -1e9;
  double max_y = -1e9;
  for (const auto& corner : corners) {
    const double dx = corner[0] - cx;
    const double dy = corner[1] - cy;
    const double rx = cx + dx * c + dy * s;
    const double ry = cy - dx * s + dy * c;
    min_x = std::min(min_x, rx);
    min_y = std::min(min_y, ry);
    max_x = std::max(max_x, rx);
    max_y = std::max(max_y, ry);
  }
  out.width = std::max(1, static_cast<int>(std::floor(max_x - min_x + 1.0 + 1e-6)));
  out.height = std::max(1, static_cast<int>(std::floor(max_y - min_y + 1.0 + 1e-6)));
  out.rgba.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
  out.coverage.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), 0);
  const double ocx = (out.width - 1) * 0.5;
  const double ocy = (out.height - 1) * 0.5;
  for (int y = 0; y < out.height; ++y) {
    for (int x = 0; x < out.width; ++x) {
      const double dx = static_cast<double>(x) - ocx;
      const double dy = static_cast<double>(y) - ocy;
      const double sx = cx + dx * c - dy * s;
      const double sy = cy + dx * s + dy * c;
      const int ix = static_cast<int>(std::floor(sx + 0.5));
      const int iy = static_cast<int>(std::floor(sy + 0.5));
      if (ix < 0 || iy < 0 || ix >= width || iy >= height) {
        continue;
      }
      if (coverage_at(coverage, width, ix, iy) == 0) {
        continue;
      }
      const std::uint8_t* sp = src + static_cast<std::size_t>(iy) * static_cast<std::size_t>(stride) +
                              static_cast<std::size_t>(ix) * 4;
      std::uint8_t* dp = out.rgba.data() +
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                         static_cast<std::size_t>(x)) *
                            4;
      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];
      dp[3] = sp[3];
      out.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                   static_cast<std::size_t>(x)] = 1;
    }
  }
  return out;
}

static SampledPixels copy_sampled(const std::uint8_t* src, int width, int height, int stride,
                           const std::uint8_t* coverage) {
  SampledPixels out;
  out.width = width;
  out.height = height;
  out.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
  out.coverage.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (coverage_at(coverage, width, x, y) == 0) {
        continue;
      }
      const std::uint8_t* sp = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                              static_cast<std::size_t>(x) * 4;
      std::uint8_t* dp = out.rgba.data() +
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x)) *
                            4;
      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];
      dp[3] = sp[3];
      out.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                   static_cast<std::size_t>(x)] = 1;
    }
  }
  return out;
}

SampledPixels shear_horizontal_nearest(const std::uint8_t* src, int width, int height, int stride,
                                       const std::uint8_t* coverage, int shift) {
  SampledPixels out;
  if (src == nullptr || width < 1 || height < 1) {
    return out;
  }
  // One row has no far edge to shift, so a shear would only pad empty pixels.
  if (height < 2 || shift == 0) {
    return copy_sampled(src, width, height, stride, coverage);
  }
  const int extra = std::abs(shift);
  out.width = width + extra;
  out.height = height;
  out.rgba.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
  out.coverage.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), 0);
  const int origin = shift < 0 ? -shift : 0;
  const int denom = std::max(1, height - 1);
  for (int y = 0; y < height; ++y) {
    const int row_shift = static_cast<int>(std::lround(static_cast<double>(shift) * y / denom));
    for (int x = 0; x < width; ++x) {
      if (coverage_at(coverage, width, x, y) == 0) {
        continue;
      }
      const int dx = x + origin + row_shift;
      if (dx < 0 || dx >= out.width) {
        continue;
      }
      const std::uint8_t* sp = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                              static_cast<std::size_t>(x) * 4;
      std::uint8_t* dp = out.rgba.data() +
                        (static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                         static_cast<std::size_t>(dx)) *
                            4;
      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];
      dp[3] = sp[3];
      out.coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                   static_cast<std::size_t>(dx)] = 1;
    }
  }
  return out;
}

SampledPixels shear_vertical_nearest(const std::uint8_t* src, int width, int height, int stride,
                                     const std::uint8_t* coverage, int shift) {
  SampledPixels out;
  if (src == nullptr || width < 1 || height < 1) {
    return out;
  }
  if (width < 2 || shift == 0) {
    return copy_sampled(src, width, height, stride, coverage);
  }
  const int extra = std::abs(shift);
  out.width = width;
  out.height = height + extra;
  out.rgba.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4, 0);
  out.coverage.assign(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height), 0);
  const int origin = shift < 0 ? -shift : 0;
  const int denom = std::max(1, width - 1);
  for (int x = 0; x < width; ++x) {
    const int col_shift = static_cast<int>(std::lround(static_cast<double>(shift) * x / denom));
    for (int y = 0; y < height; ++y) {
      if (coverage_at(coverage, width, x, y) == 0) {
        continue;
      }
      const int dy = y + origin + col_shift;
      if (dy < 0 || dy >= out.height) {
        continue;
      }
      const std::uint8_t* sp = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                              static_cast<std::size_t>(x) * 4;
      std::uint8_t* dp = out.rgba.data() +
                        (static_cast<std::size_t>(dy) * static_cast<std::size_t>(out.width) +
                         static_cast<std::size_t>(x)) *
                            4;
      dp[0] = sp[0];
      dp[1] = sp[1];
      dp[2] = sp[2];
      dp[3] = sp[3];
      out.coverage[static_cast<std::size_t>(dy) * static_cast<std::size_t>(out.width) +
                   static_cast<std::size_t>(x)] = 1;
    }
  }
  return out;
}

}  // namespace lundukepaint
