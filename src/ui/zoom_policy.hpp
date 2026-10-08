// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_ZOOM_POLICY_HPP
#define LUNDUKEPAINT_UI_ZOOM_POLICY_HPP

#include <algorithm>
#include <cmath>

namespace lundukepaint {

inline constexpr double kZoomMin = 0.125;
inline constexpr double kZoomMax = 16.0;

inline double clamp_zoom(double zoom) {
  return std::clamp(zoom, kZoomMin, kZoomMax);
}

// Zoom In, Zoom Out, and Ctrl+wheel land on these steps.
inline double snapped_zoom_step(double zoom) {
  zoom = clamp_zoom(zoom);
  static const double steps[] = {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
  double best = steps[0];
  double best_d = std::abs(zoom - best);
  for (double step : steps) {
    const double d = std::abs(zoom - step);
    if (d < best_d) {
      best = step;
      best_d = d;
    }
  }
  return best;
}

// Zoom to Fit keeps the largest ratio that fits the viewport. It is not
// snapped to the power-of-two steps.
inline double zoom_fit_ratio(double view_w, double view_h, int canvas_w, int canvas_h) {
  if (canvas_w < 1 || canvas_h < 1) {
    return 1.0;
  }
  if (view_w < 1.0) {
    view_w = 1.0;
  }
  if (view_h < 1.0) {
    view_h = 1.0;
  }
  return clamp_zoom(std::min(view_w / static_cast<double>(canvas_w),
                             view_h / static_cast<double>(canvas_h)));
}

// Display composites sample one source pixel per output pixel when the
// zoomed-out view is smaller than the document region.
inline int composite_sample_count(int src_w, int src_h, int out_w, int out_h) {
  if (src_w < 1) {
    src_w = 1;
  }
  if (src_h < 1) {
    src_h = 1;
  }
  if (out_w < 1) {
    out_w = 1;
  }
  if (out_h < 1) {
    out_h = 1;
  }
  const long long src = static_cast<long long>(src_w) * static_cast<long long>(src_h);
  const long long out = static_cast<long long>(out_w) * static_cast<long long>(out_h);
  if (out < src) {
    return static_cast<int>(out);
  }
  return static_cast<int>(src);
}

}  // namespace lundukepaint

#endif
