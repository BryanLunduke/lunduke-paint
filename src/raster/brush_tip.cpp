// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/brush_tip.hpp"

#include "raster/stroke.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

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

}  // namespace

bool brush_tip_covers(const BrushTip& tip, int dx, int dy) {
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

BrushTipStep step_brush_tip_size(int index, int size, int delta) {
  index = clamp_brush_tip_index(index);
  const BrushTip current = brush_tip_at(index);
  if (size < 1) {
    size = current.size;
  }
  struct Opt {
    int size = 0;
    int index = 0;
  };
  Opt same[kBrushTipCount];
  int n = 0;
  for (int i = 0; i < kBrushTipCount; ++i) {
    const BrushTip tip = brush_tip_at(i);
    if (tip.kind != current.kind) {
      continue;
    }
    same[n++] = Opt{tip.size, i};
  }
  for (int i = 1; i < n; ++i) {
    Opt key = same[i];
    int j = i;
    while (j > 0 && same[j - 1].size > key.size) {
      same[j] = same[j - 1];
      --j;
    }
    same[j] = key;
  }
  int pos = -1;
  for (int i = 0; i < n; ++i) {
    if (same[i].size == size) {
      pos = i;
      if (same[i].index == index) {
        break;
      }
    }
  }
  if (pos >= 0) {
    const int next = pos + (delta < 0 ? -1 : 1);
    if (next >= 0 && next < n) {
      return BrushTipStep{same[next].index, same[next].size};
    }
  }
  static constexpr int kLadder[] = {1, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32};
  constexpr int kLadderCount = static_cast<int>(sizeof(kLadder) / sizeof(kLadder[0]));
  int best = 0;
  int best_d = 1 << 30;
  for (int i = 0; i < kLadderCount; ++i) {
    const int d = size - kLadder[i];
    const int ad = d < 0 ? -d : d;
    if (ad < best_d) {
      best_d = ad;
      best = i;
    }
  }
  int next_i = best + (delta < 0 ? -1 : 1);
  if (next_i < 0) {
    next_i = 0;
  }
  if (next_i >= kLadderCount) {
    next_i = kLadderCount - 1;
  }
  const int stepped = kLadder[next_i];
  int match = index;
  for (int i = 0; i < n; ++i) {
    if (same[i].size == stepped) {
      match = same[i].index;
      break;
    }
  }
  return BrushTipStep{match, stepped};
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
      if (brush_tip_covers(tip, dx, dy)) {
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

namespace {

void smear_steps(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                 double x1, double y1, int reach, const std::function<bool(int, int)>& covers,
                 Rect* dirty) {
  if (rgba == nullptr) {
    return;
  }
  const double dx = x1 - x0;
  const double dy = y1 - y0;
  const double len = std::hypot(dx, dy);
  if (len < 0.5) {
    return;
  }
  const int steps = std::max(1, static_cast<int>(std::lround(len)));
  const int half = std::max(1, reach);
  for (int i = 1; i <= steps; ++i) {
    const double t = static_cast<double>(i) / static_cast<double>(steps);
    const double pt = static_cast<double>(i - 1) / static_cast<double>(steps);
    const int cx = static_cast<int>(std::floor(x0 + dx * t));
    const int cy = static_cast<int>(std::floor(y0 + dy * t));
    const int px = static_cast<int>(std::floor(x0 + dx * pt));
    const int py = static_cast<int>(std::floor(y0 + dy * pt));
    const int ux = cx - px;
    const int uy = cy - py;
    if (ux == 0 && uy == 0) {
      continue;
    }
    // Read every source before writing. A left-to-right loop that copies from
    // the left overwrites its own source when the drag goes right or down.
    struct Sampled {
      int dxp = 0;
      int dyp = 0;
      std::uint8_t px[4] = {};
    };
    std::vector<Sampled> samples;
    samples.reserve(static_cast<std::size_t>(half * 2 + 1) * static_cast<std::size_t>(half * 2 + 1));
    for (int oy = -half; oy <= half; ++oy) {
      for (int ox = -half; ox <= half; ++ox) {
        if (!covers(ox, oy)) {
          continue;
        }
        const int sx = cx + ox - ux;
        const int sy = cy + oy - uy;
        const int dxp = cx + ox;
        const int dyp = cy + oy;
        if (sx < 0 || sy < 0 || dxp < 0 || dyp < 0 || sx >= width || sy >= height || dxp >= width ||
            dyp >= height) {
          continue;
        }
        const std::uint8_t* s = rgba + static_cast<std::size_t>(sy) * static_cast<std::size_t>(stride) +
                               static_cast<std::size_t>(sx) * 4;
        Sampled sample;
        sample.dxp = dxp;
        sample.dyp = dyp;
        sample.px[0] = s[0];
        sample.px[1] = s[1];
        sample.px[2] = s[2];
        sample.px[3] = s[3];
        samples.push_back(sample);
      }
    }
    for (const Sampled& sample : samples) {
      std::uint8_t* d = rgba + static_cast<std::size_t>(sample.dyp) * static_cast<std::size_t>(stride) +
                       static_cast<std::size_t>(sample.dxp) * 4;
      d[0] = sample.px[0];
      d[1] = sample.px[1];
      d[2] = sample.px[2];
      d[3] = sample.px[3];
      if (dirty != nullptr) {
        *dirty = rect_union(*dirty, Rect{sample.dxp, sample.dyp, 1, 1});
      }
    }
  }
}

}  // namespace

void smear_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                     double x1, double y1, const BrushTip& tip, Rect* dirty) {
  const int reach = std::max(1, tip.size) / 2 + 1;
  smear_steps(rgba, width, height, stride, x0, y0, x1, y1, reach,
              [&](int dx, int dy) { return brush_tip_covers(tip, dx, dy); }, dirty);
}

void smear_custom_brush(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                        double x1, double y1, const CustomBrush& brush, Rect* dirty) {
  if (brush.empty()) {
    return;
  }
  const int reach = std::max(brush.width, brush.height) / 2 + 1;
  const int left = -(brush.width / 2);
  const int top = -(brush.height / 2);
  smear_steps(rgba, width, height, stride, x0, y0, x1, y1, reach,
              [&](int dx, int dy) {
                const int bx = dx - left;
                const int by = dy - top;
                if (bx < 0 || by < 0 || bx >= brush.width || by >= brush.height) {
                  return false;
                }
                const std::uint8_t* p =
                    brush.rgba.data() + (static_cast<std::size_t>(by) * brush.width + bx) * 4;
                return p[3] != 0;
              },
              dirty);
}

}  // namespace lundukepaint
