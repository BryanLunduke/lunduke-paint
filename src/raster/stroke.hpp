// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_STROKE_HPP
#define LUNDUKEPAINT_RASTER_STROKE_HPP

#include "raster/types.hpp"

#include <cstdint>

namespace lundukepaint {

// Hard square stamp, no anti-alias. size is N×N pixels.
void stamp_square(std::uint8_t* rgba, int width, int height, int stride, int cx, int cy,
                  int size, Color color, Rect* dirty);

// Round brush stamp. If antialias is true, coverage is blended (straight alpha).
void stamp_round(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                 int size, Color color, bool antialias, Rect* dirty);

// Bresenham / sampled line of square stamps (pencil).
void stroke_pencil(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                   int y1, int size, Color color, Rect* dirty);

// Sampled line of round stamps (brush / eraser).
void stroke_brush(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                  double x1, double y1, int size, Color color, bool antialias, Rect* dirty);

// Replace target-similar pixels inside a round stamp with replacement (BG / transparent).
void color_erase_stamp(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                       int size, Color target, Color replacement, int tolerance, Rect* dirty);
void color_erase_stroke(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                        double x1, double y1, int size, Color target, Color replacement,
                        int tolerance, Rect* dirty);

// Random dots in a disk. density is 1–100. rng is a 32-bit LCG state.
void spray_dots(std::uint8_t* rgba, int width, int height, int stride, double cx, double cy,
                int radius, int density, Color color, std::uint32_t* rng, Rect* dirty);

// Dots stepped along the segment. Step is about radius/4 so a fast drag does not
// leave gaps. density is the same 1–100 scale as spray_dots (the spray tool
// keeps it at 40; the left rail sets radius only).
void stroke_spray(std::uint8_t* rgba, int width, int height, int stride, double x0, double y0,
                  double x1, double y1, int radius, int density, Color color, std::uint32_t* rng,
                  Rect* dirty);

}  // namespace lundukepaint

#endif
