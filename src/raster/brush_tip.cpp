// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/brush_tip.hpp"

#include "raster/stroke.hpp"

#include <algorithm>
#include <cmath>

namespace lundukepaint {
namespace {

constexpr BrushTip kTips[kBrushTipCount] = {
    {BrushTipKind::Round, 1},  {BrushTipKind::Round, 2},  {BrushTipKind::Round, 4},
    {BrushTipKind::Round, 8},  {BrushTipKind::Square, 2}, {BrushTipKind::Square, 4},
    {BrushTipKind::Square, 6}, {BrushTipKind::Square, 8}, {BrushTipKind::Slash, 3},
    {BrushTipKind::Slash, 5},  {BrushTipKind::Slash, 8},  {BrushTipKind::Backslash, 3},
    {BrushTipKind::Backslash, 5}, {BrushTipKind::Backslash, 8}, {BrushTipKind::HBar, 5},
    {BrushTipKind::Cross, 7},
};

void put_px(std::uint8_t* rgba, int width, int height, int stride, int x, int y, Color color,
            Rect* dirty, const Pattern* pattern, Color pattern_bg) {
  if (x < 0 || y < 0 || x >= width || y >= height || rgba == nullptr) {
    return;
  }
  if (pattern != nullptr) {
    color = pattern->color_at(x, y, color, pattern_bg);
  }
  std::uint8_t* p = rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  p[0] = color.r;
  p[1] = color.g;
  p[2] = color.b;
  p[3] = color.a;
  if (dirty != nullptr) {
    *dirty = rect_union(*dirty, Rect{x, y, 1, 1});
  }
}

bool tip_covers(const BrushTip& tip, int dx, int dy) {
  const int s = std::max(1, tip.size);
  const int half = s / 2;
  switch (tip.kind) {
    case BrushTipKind::Round: {
      const double r = static_cast<double>(s) * 0.5;
      const double ddx = static_cast<double>(dx) + 0.5;
      const double ddy = static_cast<double>(dy) + 0.5;
      return (ddx * ddx + ddy * ddy) <= r * r + 0.25;
    }
    case BrushTipKind::Square:
      return dx >= -half && dx < -half + s && dy >= -half && dy < -half + s;
    case BrushTipKind::Slash: {
      // Diagonal from lower-left to upper-right, thickness ~1–2 px.
      const int t = std::max(1, s / 4);
      return std::abs(dx + dy) <= t && std::abs(dx) <= half && std::abs(dy) <= half;
    }
    case BrushTipKind::Backslash: {
      const int t = std::max(1, s / 4);
      return std::abs(dx - dy) <= t && std::abs(dx) <= half && std::abs(dy) <= half;
    }
    case BrushTipKind::HBar: {
      const int t = std::max(1, s / 5);
      return std::abs(dy) <= t && dx >= -half && dx <= half;
    }
    case BrushTipKind::VBar: {
      const int t = std::max(1, s / 5);
      return std::abs(dx) <= t && dy >= -half && dy <= half;
    }
    case BrushTipKind::Cross: {
      const int t = std::max(1, s / 5);
      const bool h = std::abs(dy) <= t && dx >= -half && dx <= half;
      const bool v = std::abs(dx) <= t && dy >= -half && dy <= half;
      return h || v;
    }
  }
  return false;
}

}  // namespace

const BrushTip& brush_tip_at(int index) {
  return kTips[clamp_brush_tip_index(index)];
}

int clamp_brush_tip_index(int index) {
  if (index < 0) {
    return 0;
  }
  if (index >= kBrushTipCount) {
    return kBrushTipCount - 1;
  }
  return index;
}

void stamp_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                     const BrushTip& tip, Color color, Rect* dirty, const Pattern* pattern,
                     Color pattern_bg) {
  if (rgba == nullptr) {
    return;
  }
  // Round and square tips keep the solid stamps when no pattern is selected.
  if (pattern == nullptr && tip.kind == BrushTipKind::Round) {
    stamp_round(rgba, width, height, stride, cx, cy, tip.size, color, false, dirty);
    return;
  }
  if (pattern == nullptr && tip.kind == BrushTipKind::Square) {
    stamp_square(rgba, width, height, stride, static_cast<int>(std::floor(cx)),
                 static_cast<int>(std::floor(cy)), tip.size, color, dirty);
    return;
  }

  const int icx = static_cast<int>(std::floor(cx));
  const int icy = static_cast<int>(std::floor(cy));
  const int half = std::max(1, tip.size) / 2 + 1;
  for (int dy = -half; dy <= half; ++dy) {
    for (int dx = -half; dx <= half; ++dx) {
      if (tip_covers(tip, dx, dy)) {
        put_px(rgba, width, height, stride, icx + dx, icy + dy, color, dirty, pattern, pattern_bg);
      }
    }
  }
}

void stroke_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                      double x1, double y1, const BrushTip& tip, Color color, Rect* dirty,
                      const Pattern* pattern, Color pattern_bg) {
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len = std::sqrt(dx * dx + dy * dy);
  if (len < 0.001) {
    stamp_brush_tip(rgba, width, height, stride, x0, y0, tip, color, dirty, pattern, pattern_bg);
    return;
  }
  const double step = std::max(1.0, static_cast<double>(tip.size) * 0.25);
  const int n = static_cast<int>(std::ceil(len / step));
  for (int i = 0; i <= n; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(n);
    stamp_brush_tip(rgba, width, height, stride, x0 + dx * t, y0 + dy * t, tip, color, dirty,
                    pattern, pattern_bg);
  }
}

}  // namespace lundukepaint
