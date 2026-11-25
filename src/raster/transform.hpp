// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_TRANSFORM_HPP
#define LUNDUKEPAINT_RASTER_TRANSFORM_HPP

#include "raster/types.hpp"

#include <cstdint>
#include <vector>

namespace lundukepaint {

void flip_h(std::uint8_t* rgba, int width, int height, int stride);
void flip_v(std::uint8_t* rgba, int width, int height, int stride);
void rotate_180(std::uint8_t* rgba, int width, int height, int stride);

// dest size must be (src_height × src_width). dest(x,y) = src(y, src_h-1-x).
void rotate_90_cw(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                  std::uint8_t* dest, int dest_stride);
void rotate_90_ccw(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                   std::uint8_t* dest, int dest_stride);

void scale_nearest(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                   std::uint8_t* dest, int dest_w, int dest_h, int dest_stride);
void scale_bilinear(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                    std::uint8_t* dest, int dest_w, int dest_h, int dest_stride);

void resize_canvas(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                   std::uint8_t* dest, int dest_w, int dest_h, int dest_stride, Color fill);

void crop_rect(const std::uint8_t* src, int src_w, int src_h, int src_stride, Rect rect,
               std::uint8_t* dest, int dest_stride);

// Trim uniform-color borders. Returns a 1×1 rect if the whole buffer is uniform.
Rect autocrop_bounds(const std::uint8_t* rgba, int width, int height, int stride);

// A layer buffer plus its document offset. Image transforms keep this placement
// so a layer smaller than the canvas is not read as if it were canvas-sized,
// and a non-zero offset is not cleared unless the pixels were rebased into the
// new canvas and the picture still matches.
struct PlacedPixels {
  int width = 1;
  int height = 1;
  int offset_x = 0;
  int offset_y = 0;
  std::vector<std::uint8_t> pixels;
};

PlacedPixels place_flip_h(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                          int oy, int canvas_w, int canvas_h);
PlacedPixels place_flip_v(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                          int oy, int canvas_w, int canvas_h);
PlacedPixels place_rotate_180(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                              int oy, int canvas_w, int canvas_h);
PlacedPixels place_rotate_90_cw(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                                int ox, int oy, int canvas_w, int canvas_h);
PlacedPixels place_rotate_90_ccw(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                                 int ox, int oy, int canvas_w, int canvas_h);
PlacedPixels place_scale(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox,
                         int oy, int canvas_w, int canvas_h, int new_w, int new_h, bool nearest);
PlacedPixels place_crop(const std::uint8_t* src, int src_w, int src_h, int src_stride, int ox, int oy,
                        Rect crop);
PlacedPixels place_resize_canvas(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                                 int ox, int oy, int canvas_w, int canvas_h, int new_w, int new_h,
                                 Color fill);

// Nearest-neighbor image plus an optional coverage grid (empty means solid).
struct SampledPixels {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
  std::vector<std::uint8_t> coverage;
};

// Clockwise degrees. The result is the bounding box of the rotated rectangle.
// Samples that fall outside the source are transparent and uncovered.
SampledPixels rotate_nearest_angle(const std::uint8_t* src, int width, int height, int stride,
                                   const std::uint8_t* coverage, double degrees_clockwise);

// Horizontal shear: the bottom row shifts by `shift` pixels (positive = right).
// Vertical shear: the right column shifts by `shift` pixels (positive = down).
SampledPixels shear_horizontal_nearest(const std::uint8_t* src, int width, int height, int stride,
                                       const std::uint8_t* coverage, int shift);
SampledPixels shear_vertical_nearest(const std::uint8_t* src, int width, int height, int stride,
                                     const std::uint8_t* coverage, int shift);

}  // namespace lundukepaint

#endif
