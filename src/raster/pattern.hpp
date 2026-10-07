// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_PATTERN_HPP
#define LUNDUKEPAINT_RASTER_PATTERN_HPP

#include "raster/types.hpp"

#include <cstddef>
#include <cstdint>

namespace lundukepaint {

// Classic 8×8 MacPaint-style dither pattern. bits[row] bit7 = leftmost pixel.
struct Pattern {
  std::uint8_t rows[8]{};

  bool bit_at(int x, int y) const {
    const int row = ((y % 8) + 8) % 8;
    const int col = ((x % 8) + 8) % 8;
    return (rows[row] & static_cast<std::uint8_t>(0x80u >> col)) != 0;
  }

  Color color_at(int x, int y, Color fg, Color bg) const {
    return bit_at(x, y) ? fg : bg;
  }
};

inline constexpr int kPatternCount = 38;

const Pattern& pattern_at(int index);
int clamp_pattern_index(int index);

// Apply an 8×8 pattern over a 0/255 mask. The mask covers
// [mask_x, mask_x+mask_w) × [mask_y, mask_y+mask_h) in buffer space.
void apply_pattern_mask(std::uint8_t* rgba, int width, int height, int stride,
                        const std::uint8_t* mask, int mask_x, int mask_y, int mask_w, int mask_h,
                        const Pattern& pattern, Color fg, Color bg, Rect* dirty);

}  // namespace lundukepaint

#endif
