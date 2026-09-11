// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_BRUSH_TIP_HPP
#define LUNDUKEPAINT_RASTER_BRUSH_TIP_HPP

#include "raster/types.hpp"

#include <cstdint>

namespace lundukepaint {

// MacPaint-inspired brush tips (compact left-rail picker).
enum class BrushTipKind : int {
  Round = 0,
  Square,
  Slash,       // forward slash diagonal
  Backslash,   // backslash diagonal
  HBar,
  VBar,
  Cross,
};

struct BrushTip {
  BrushTipKind kind = BrushTipKind::Round;
  int size = 8;  // diameter / extent in pixels
};

inline constexpr int kBrushTipCount = 16;

const BrushTip& brush_tip_at(int index);
int clamp_brush_tip_index(int index);

// Stamp one tip centered at (cx, cy). Hard edges (MacPaint-like).
void stamp_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                     const BrushTip& tip, Color color, Rect* dirty);

// Sampled stroke of tip stamps along a segment.
void stroke_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                      double x1, double y1, const BrushTip& tip, Color color, Rect* dirty);

}  // namespace lundukepaint

#endif
