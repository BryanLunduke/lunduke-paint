// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_EFFECTS_HPP
#define LUNDUKEPAINT_RASTER_EFFECTS_HPP

#include "raster/types.hpp"

#include <cstddef>
#include <cstdint>

namespace lundukepaint {

// Separable box blur keeps a ring of horizontal sums, not a full-frame buffer.
inline std::size_t box_blur_scratch_bytes(int span, int radius) {
  if (span < 1) {
    span = 1;
  }
  if (radius < 1) {
    radius = 1;
  }
  if (radius > 16) {
    radius = 16;
  }
  const int win = radius * 2 + 1;
  return static_cast<std::size_t>(win) * static_cast<std::size_t>(span) * 4u * sizeof(std::uint32_t);
}

struct BlurWork {
  std::size_t scratch_bytes = 0;
  int pixels_written = 0;
};

// In-place adjustments. Alpha is left unchanged.
void invert_rgba(std::uint8_t* rgba, int width, int height, int stride);
void grayscale_rgba(std::uint8_t* rgba, int width, int height, int stride);

// brightness and contrast are -100..100.
void brightness_contrast_rgba(std::uint8_t* rgba, int width, int height, int stride,
                              int brightness, int contrast);

// hue is -180..180 degrees; saturation is -100..100.
void hue_saturation_rgba(std::uint8_t* rgba, int width, int height, int stride, int hue,
                         int saturation);

// levels is 2..16.
void posterize_rgba(std::uint8_t* rgba, int width, int height, int stride, int levels);

// src and dest must not alias. radius is 1..16. Blurs the whole buffer.
void box_blur_rgba(const std::uint8_t* src, int width, int height, int src_stride,
                   std::uint8_t* dest, int dest_stride, int radius);

// Same blur, writing only `region` (selection or dirty bounds). Pixels outside
// `region` are left untouched. `work` records the scratch size when non-null.
void box_blur_rect(const std::uint8_t* src, int width, int height, int src_stride,
                   std::uint8_t* dest, int dest_stride, int radius, Rect region, BlurWork* work = nullptr);
void sharpen_rgba(const std::uint8_t* src, int width, int height, int src_stride,
                  std::uint8_t* dest, int dest_stride);
void emboss_rgba(const std::uint8_t* src, int width, int height, int src_stride,
                 std::uint8_t* dest, int dest_stride);

// MacPaint Trace Edges: marks that touch the paper stay (drawn in `ink`);
// interior marks and the paper itself become `paper`. A mark differs from paper.
void trace_edges_rgba(std::uint8_t* rgba, int width, int height, int stride, Color ink, Color paper);

// Hard halftone. Each matrix cell is `cell` pixels (1..16). Output is only
// `ink` or `paper` — a newspaper screen, not a blur.
enum class HalftoneScreen { Bayer, Clustered };
void halftone_rgba(std::uint8_t* rgba, int width, int height, int stride, Color ink, Color paper,
                   int cell, HalftoneScreen screen);

// Swap each pixel with one random neighbor inside the Chebyshev radius.
// `seed` makes the shuffle repeatable (0 is treated as 1).
void diffuse_rgba(std::uint8_t* rgba, int width, int height, int stride, int radius,
                  std::uint32_t seed);

// Hard silhouette offset. Marks (pixels that differ from `paper`) stay.
// The offset copy is painted in `shadow` only where the source was not a mark.
void drop_shadow_rgba(std::uint8_t* rgba, int width, int height, int stride, int dx, int dy,
                      Color shadow, Color paper);

enum class WindDirection { Right, Left, Down, Up };

// Hard streaks. An edge pixel is copied 1..`strength` pixels along `direction`.
void wind_rgba(std::uint8_t* rgba, int width, int height, int stride, WindDirection direction,
               int strength, std::uint32_t seed);

// Every odd row is darkened. `percent` is 0..100. Even rows and alpha stay.
void crt_scanlines_rgba(std::uint8_t* rgba, int width, int height, int stride, int percent);

}  // namespace lundukepaint

#endif
