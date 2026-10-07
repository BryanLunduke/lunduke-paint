// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_FILL_HPP
#define LUNDUKEPAINT_RASTER_FILL_HPP

#include "raster/types.hpp"

#include <cstdint>
#include <vector>

namespace lundukepaint {

// Contiguous flood fill. tolerance is Chebyshev distance in RGBA (0 = exact).
// Writes into rgba (straight alpha, stride = width * 4 unless given).
void flood_fill(std::uint8_t* rgba, int width, int height, int stride, int x, int y,
                Color replacement, int tolerance, Rect* dirty);

// Contiguous color select (Chebyshev). `mask` is tight (bounds.w × bounds.h),
// not a full-canvas buffer. *bounds is that rectangle in buffer space (empty if none).
void flood_mask(const std::uint8_t* rgba, int width, int height, int stride, int x, int y,
                int tolerance, std::vector<std::uint8_t>& mask, Rect* bounds);

// Times flood fill or merge-down has pumped the GTK main loop. Both must leave
// this at zero: document mutation must not re-enter the main context.
int mutation_main_loop_pumps();

}  // namespace lundukepaint

#endif
