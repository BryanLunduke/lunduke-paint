// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_BRUSH_TIP_HPP
#define LUNDUKEPAINT_RASTER_BRUSH_TIP_HPP

#include "raster/custom_brush.hpp"
#include "raster/pattern.hpp"
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

// `[` / `]` step the size of the current tip shape. `size` <= 0 means the
// size stored on that tip. The returned index stays on the same shape when a
// picker cell has that size.
struct BrushTipStep {
  int index = 0;
  int size = 1;
};
BrushTipStep step_brush_tip_size(int index, int size, int delta);
// True when (dx, dy) from the tip center is inside the hard tip.
bool brush_tip_covers(const BrushTip& tip, int dx, int dy);

// Hard smear: each step copies the pixels one step behind the motion into the
// tip. No blending. A zero-length move does nothing.
void smear_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                     double x1, double y1, const BrushTip& tip, Rect* dirty);

// Same drag, masked by the custom brush's opaque pixels.
void smear_custom_brush(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                        double x1, double y1, const CustomBrush& brush, Rect* dirty);

// Stamp one tip centered at (cx, cy). Hard edges (MacPaint-like).
void stamp_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                     const BrushTip& tip, Color color, Rect* dirty, const Pattern* pattern = nullptr,
                     Color pattern_bg = Color::white());

// Sampled stroke of tip stamps along a segment.
void stroke_brush_tip(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                      double x1, double y1, const BrushTip& tip, Color color, Rect* dirty,
                      const Pattern* pattern = nullptr, Color pattern_bg = Color::white());

}  // namespace lundukepaint

#endif
