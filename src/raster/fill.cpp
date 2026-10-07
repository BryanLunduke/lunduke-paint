// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/fill.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lundukepaint {
namespace {

Color get_pixel(const std::uint8_t* rgba, int stride, int x, int y) {
  const std::uint8_t* p = rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

void set_pixel(std::uint8_t* rgba, int stride, int x, int y, Color c) {
  std::uint8_t* p = rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

enum Seen : std::uint8_t { kUnseen = 0, kReject = 1, kMatch = 2, kFilled = 3 };

struct Span {
  int y = 0;
  int x = 0;
};

// Scanline flood. Rejected pixels are marked the first time they are tested.
// This does not pump the GTK main loop: a nested iteration can free the layer
// buffer the fill is writing.
// `paint` writes replacement; otherwise only the filled state is recorded.
bool scanline_flood(const std::uint8_t* src, std::uint8_t* dest, int width, int height, int stride,
                    int x, int y, int tolerance, Color replacement, bool paint, Rect* bounds,
                    std::vector<std::uint8_t>& state) {
  if (bounds != nullptr) {
    *bounds = {};
  }
  state.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), kUnseen);
  if (src == nullptr || width < 1 || height < 1 || stride < width * 4) {
    return false;
  }
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return false;
  }
  if (tolerance < 0) {
    tolerance = 0;
  }
  const Color seed = get_pixel(src, stride, x, y);

  auto classify = [&](int cx, int cy) -> std::uint8_t {
    if (cx < 0 || cy < 0 || cx >= width || cy >= height) {
      return kReject;
    }
    const std::size_t idx = static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(cx);
    if (state[idx] != kUnseen) {
      return state[idx];
    }
    const Color current = get_pixel(src, stride, cx, cy);
    if (color_chebyshev(current, seed) > tolerance) {
      state[idx] = kReject;
      return kReject;
    }
    state[idx] = kMatch;
    return kMatch;
  };

  if (classify(x, y) != kMatch) {
    return false;
  }

  int minx = width;
  int miny = height;
  int maxx = -1;
  int maxy = -1;
  std::vector<Span> stack;
  stack.push_back(Span{y, x});

  while (!stack.empty()) {
    const Span span = stack.back();
    stack.pop_back();
    int left = span.x;
    while (classify(left - 1, span.y) == kMatch) {
      --left;
    }
    int right = left;
    while (classify(right, span.y) == kMatch) {
      const std::size_t idx = static_cast<std::size_t>(span.y) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(right);
      state[idx] = kFilled;
      if (paint && dest != nullptr) {
        set_pixel(dest, stride, right, span.y, replacement);
      }
      if (right < minx) {
        minx = right;
      }
      if (span.y < miny) {
        miny = span.y;
      }
      if (right > maxx) {
        maxx = right;
      }
      if (span.y > maxy) {
        maxy = span.y;
      }
      ++right;
    }
    --right;
    if (right < left) {
      continue;
    }
    for (int ny : {span.y - 1, span.y + 1}) {
      int sx = left;
      while (sx <= right) {
        if (classify(sx, ny) == kMatch) {
          const int start = sx;
          while (sx <= right && classify(sx, ny) == kMatch) {
            ++sx;
          }
          stack.push_back(Span{ny, start});
        } else {
          ++sx;
        }
      }
    }
  }

  if (maxx < minx || bounds == nullptr) {
    return maxx >= minx;
  }
  *bounds = {minx, miny, maxx - minx + 1, maxy - miny + 1};
  return true;
}

}  // namespace

int mutation_main_loop_pumps() {
  return 0;
}

void flood_fill(std::uint8_t* rgba, int width, int height, int stride, int x, int y,
                Color replacement, int tolerance, Rect* dirty) {
  if (dirty != nullptr) {
    *dirty = {};
  }
  if (rgba == nullptr || width < 1 || height < 1 || stride < width * 4) {
    return;
  }
  if (x < 0 || y < 0 || x >= width || y >= height) {
    return;
  }
  if (tolerance < 0) {
    tolerance = 0;
  }
  const Color seed = get_pixel(rgba, stride, x, y);
  if (color_chebyshev(seed, replacement) <= 0) {
    return;
  }
  std::vector<std::uint8_t> state;
  Rect bounds{};
  if (!scanline_flood(rgba, rgba, width, height, stride, x, y, tolerance, replacement, true, &bounds,
                      state)) {
    return;
  }
  if (dirty != nullptr) {
    *dirty = bounds;
  }
}

void flood_mask(const std::uint8_t* rgba, int width, int height, int stride, int x, int y,
                int tolerance, std::vector<std::uint8_t>& mask, Rect* bounds) {
  mask.clear();
  if (bounds != nullptr) {
    *bounds = {};
  }
  std::vector<std::uint8_t> state;
  Rect tight{};
  if (!scanline_flood(rgba, nullptr, width, height, stride, x, y, tolerance, Color::transparent(),
                      false, &tight, state)) {
    return;
  }
  mask.assign(static_cast<std::size_t>(tight.w) * static_cast<std::size_t>(tight.h), 0);
  for (int yy = 0; yy < tight.h; ++yy) {
    for (int xx = 0; xx < tight.w; ++xx) {
      const std::size_t src = static_cast<std::size_t>(tight.y + yy) * static_cast<std::size_t>(width) +
                              static_cast<std::size_t>(tight.x + xx);
      if (state[src] == kFilled) {
        mask[static_cast<std::size_t>(yy) * static_cast<std::size_t>(tight.w) +
             static_cast<std::size_t>(xx)] = 255;
      }
    }
  }
  if (bounds != nullptr) {
    *bounds = tight;
  }
}

}  // namespace lundukepaint
