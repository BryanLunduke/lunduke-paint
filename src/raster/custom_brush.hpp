// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_CUSTOM_BRUSH_HPP
#define LUNDUKEPAINT_RASTER_CUSTOM_BRUSH_HPP

#include "raster/types.hpp"

#include <cstdint>
#include <vector>

namespace lundukepaint {

// Deluxe Paint style: a picked-up image stamped by the brush. Alpha 0 is
// transparent. Pixels equal to the background color are stored with alpha 0.
struct CustomBrush {
  int width = 0;
  int height = 0;
  // 0 keeps the automatic stamp step. A positive value is the gap in pixels.
  int spacing = 0;
  std::vector<std::uint8_t> rgba;

  bool empty() const { return width < 1 || height < 1 || rgba.empty(); }
};

// Copies a rectangle and punches out `background` (and existing alpha 0).
// Images larger than 256px on a side are nearest-neighbor scaled to fit.
CustomBrush custom_brush_from_rgba(const std::uint8_t* rgba, int width, int height, int stride,
                                   Color background);

void stamp_custom_brush(std::uint8_t* rgba, int width, int height, int stride, int cx, int cy,
                        const CustomBrush& brush, Rect* dirty);

void stroke_custom_brush(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                         double x1, double y1, const CustomBrush& brush, Rect* dirty);

// Workshop edits. Spacing is kept. Results stay at most 256 pixels on a side.
void custom_brush_flip_h(CustomBrush& brush);
void custom_brush_flip_v(CustomBrush& brush);
void custom_brush_rotate_90(CustomBrush& brush);
void custom_brush_scale(CustomBrush& brush, int numerator, int denominator);
// Opaque pixels take `ink`. Transparent pixels stay the background key.
void custom_brush_remap(CustomBrush& brush, Color ink);

}  // namespace lundukepaint

#endif
