// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_SHAPES_HPP
#define LUNDUKEPAINT_RASTER_SHAPES_HPP

#include "raster/pattern.hpp"
#include "raster/types.hpp"

#include <cstdint>

namespace lundukepaint {

enum class ShapeFillMode { Stroke, Fill, Both };

void draw_line(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
               int y1, int thickness, Color color, bool antialias, Rect* dirty);

void draw_rectangle(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                    int y1, int thickness, Color color, ShapeFillMode mode, bool antialias,
                    Rect* dirty, const Pattern* pattern = nullptr,
                    Color pattern_bg = Color::white());

void draw_ellipse(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0, int x1,
                  int y1, int thickness, Color color, ShapeFillMode mode, bool antialias,
                  Rect* dirty, const Pattern* pattern = nullptr,
                  Color pattern_bg = Color::white());

void draw_rounded_rect(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0,
                       int x1, int y1, int thickness, int radius, Color color, ShapeFillMode mode,
                       bool antialias, Rect* dirty, const Pattern* pattern = nullptr,
                       Color pattern_bg = Color::white());

void draw_polyline(std::uint8_t* rgba, int width, int height, int stride, const int* xs,
                   const int* ys, int count, int thickness, Color color, bool antialias,
                   Rect* dirty);

void draw_polygon(std::uint8_t* rgba, int width, int height, int stride, const int* xs,
                  const int* ys, int count, int thickness, Color color, ShapeFillMode mode,
                  bool antialias, Rect* dirty, const Pattern* pattern = nullptr,
                  Color pattern_bg = Color::white());

void draw_cubic_bezier(std::uint8_t* rgba, int width, int height, int stride, int x0, int y0,
                       int x1, int y1, int x2, int y2, int x3, int y3, int thickness, Color color,
                       bool antialias, Rect* dirty);

void fill_polygon_mask(std::uint8_t* mask, int width, int height, const int* xs, const int* ys,
                       int count);

void fill_ellipse_mask(std::uint8_t* mask, int width, int height);

void stroke_polyline_mask(std::uint8_t* mask, int width, int height, const int* xs, const int* ys,
                          int count, bool closed);

void constrain_line_45(int x0, int y0, int* x1, int* y1);
void constrain_square(int x0, int y0, int* x1, int* y1);

}  // namespace lundukepaint

#endif
