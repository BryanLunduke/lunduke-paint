// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_GRADIENT_HPP
#define LUNDUKEPAINT_RASTER_GRADIENT_HPP

#include "raster/types.hpp"

#include <cstdint>

namespace lundukepaint {

enum class GradientKind { Linear, Radial };

// Two-color fill with an 8×8 Bayer matrix. Pixels inside the mask (255 = paint)
// become c0 or c1. A null mask paints every pixel. Coordinates are buffer space.
void gradient_dither(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                     int y1, Color c0, Color c1, GradientKind kind, const std::uint8_t* mask,
                     int mask_x, int mask_y, int mask_w, int mask_h, Rect* dirty);

}  // namespace lundukepaint

#endif
