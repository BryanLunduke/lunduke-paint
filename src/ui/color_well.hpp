// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_COLOR_WELL_HPP
#define LUNDUKEPAINT_UI_COLOR_WELL_HPP

namespace lundukepaint {

// Classic overlapping foreground / background wells. The foreground square
// sits on top and wins the overlap; the background square peeks out at the
// bottom-right.
enum class WellHit { None, Foreground, Background };

struct ColorWellGeom {
  static constexpr int kWidth = 48;
  static constexpr int kHeight = 42;
  static constexpr int kFgX = 2;
  static constexpr int kFgY = 2;
  static constexpr int kFg = 26;
  static constexpr int kBgX = 18;
  static constexpr int kBgY = 14;
  static constexpr int kBg = 22;
};

inline WellHit color_well_hit(double x, double y) {
  const bool fg = x >= ColorWellGeom::kFgX && y >= ColorWellGeom::kFgY &&
                  x < ColorWellGeom::kFgX + ColorWellGeom::kFg &&
                  y < ColorWellGeom::kFgY + ColorWellGeom::kFg;
  if (fg) {
    return WellHit::Foreground;
  }
  const bool bg = x >= ColorWellGeom::kBgX && y >= ColorWellGeom::kBgY &&
                  x < ColorWellGeom::kBgX + ColorWellGeom::kBg &&
                  y < ColorWellGeom::kBgY + ColorWellGeom::kBg;
  if (bg) {
    return WellHit::Background;
  }
  return WellHit::None;
}

}  // namespace lundukepaint

#endif
