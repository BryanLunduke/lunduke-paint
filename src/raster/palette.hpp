// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_PALETTE_HPP
#define LUNDUKEPAINT_RASTER_PALETTE_HPP

#include "raster/types.hpp"

#include <cstdint>

namespace lundukepaint {

struct RetroPalette {
  const char* name;
  const Color* colors;
  int count;
  int width;
  int height;
};

int retro_palette_count();
const RetroPalette& retro_palette_at(int index);

enum class DitherKind { None, Ordered, FloydSteinberg };

// Replaces RGB with the nearest palette color. Alpha is left alone.
void quantize_palette(std::uint8_t* rgba, int width, int height, int stride, const Color* colors,
                      int count, DitherKind kind);

}  // namespace lundukepaint

#endif
