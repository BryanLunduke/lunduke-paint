// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_TYPES_HPP
#define LUNDUKEPAINT_RASTER_TYPES_HPP

#include <algorithm>
#include <cstdint>

namespace lundukepaint {

struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  static Color black() { return {0, 0, 0, 255}; }
  static Color white() { return {255, 255, 255, 255}; }
  static Color transparent() { return {0, 0, 0, 0}; }

  bool operator==(const Color& other) const {
    return r == other.r && g == other.g && b == other.b && a == other.a;
  }

  bool operator!=(const Color& other) const { return !(*this == other); }

  bool fully_transparent() const { return a == 0; }
};

inline int color_chebyshev(Color a, Color b) {
  const int dr = std::abs(static_cast<int>(a.r) - static_cast<int>(b.r));
  const int dg = std::abs(static_cast<int>(a.g) - static_cast<int>(b.g));
  const int db = std::abs(static_cast<int>(a.b) - static_cast<int>(b.b));
  const int da = std::abs(static_cast<int>(a.a) - static_cast<int>(b.a));
  return std::max(std::max(dr, dg), std::max(db, da));
}

struct Rect {
  int x = 0;
  int y = 0;
  int w = 0;
  int h = 0;

  bool empty() const { return w <= 0 || h <= 0; }

  int x2() const { return x + w; }
  int y2() const { return y + h; }

  bool contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }

  static Rect from_points(int x0, int y0, int x1, int y1) {
    const int left = std::min(x0, x1);
    const int top = std::min(y0, y1);
    return {left, top, std::abs(x1 - x0) + 1, std::abs(y1 - y0) + 1};
  }
};

inline Rect rect_intersect(Rect a, Rect b) {
  const int x0 = std::max(a.x, b.x);
  const int y0 = std::max(a.y, b.y);
  const int x1 = std::min(a.x2(), b.x2());
  const int y1 = std::min(a.y2(), b.y2());
  if (x1 <= x0 || y1 <= y0) {
    return {};
  }
  return {x0, y0, x1 - x0, y1 - y0};
}

inline Rect rect_union(Rect a, Rect b) {
  if (a.empty()) {
    return b;
  }
  if (b.empty()) {
    return a;
  }
  const int x0 = std::min(a.x, b.x);
  const int y0 = std::min(a.y, b.y);
  const int x1 = std::max(a.x2(), b.x2());
  const int y1 = std::max(a.y2(), b.y2());
  return {x0, y0, x1 - x0, y1 - y0};
}

inline Rect rect_clamp(Rect r, int width, int height) {
  return rect_intersect(r, Rect{0, 0, width, height});
}

constexpr int kSoftMaxSide = 8192;
constexpr int kHardMaxSide = 16384;
constexpr int kDefaultWidth = 640;
constexpr int kDefaultHeight = 480;
constexpr int kDefaultUndoDepth = 50;
constexpr int kMaxUndoDepth = 200;
constexpr int kPatchTile = 32;
constexpr int kSoftMaxLayers = 64;
// OpenRaster loads above this fail closed (no "continue anyway" dialog).
constexpr int kHardMaxLayers = 1024;
constexpr int kThumbWidth = 48;
constexpr int kThumbHeight = 36;

}  // namespace lundukepaint

#endif
