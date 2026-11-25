// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_RASTER_GRID_SNAP_HPP
#define LUNDUKEPAINT_RASTER_GRID_SNAP_HPP

#include <cmath>

namespace lundukepaint {

// Snap a canvas-space coordinate to a grid of `spacing` image pixels.
// Callers pass coordinates that have already been divided by the zoom, so
// the grid stays the same size at any magnification.
inline int snap_canvas_coord(double canvas, int spacing) {
  if (spacing <= 1) {
    return static_cast<int>(std::floor(canvas));
  }
  const double g = static_cast<double>(spacing);
  return static_cast<int>(std::floor(canvas / g + 0.5)) * spacing;
}

inline double canvas_from_widget(double widget, double origin, double zoom) {
  const double z = zoom < 1e-6 ? 1e-6 : zoom;
  return (widget - origin) / z;
}

}  // namespace lundukepaint

#endif
