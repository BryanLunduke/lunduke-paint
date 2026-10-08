// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_TOOLS_SELECTION_XFORM_HPP
#define LUNDUKEPAINT_TOOLS_SELECTION_XFORM_HPP

#include "raster/types.hpp"
#include "tools/tool.hpp"

#include <cairomm/context.h>

#include <cstdint>
#include <vector>

namespace lundukepaint {

class Selection;

enum class SelHandle { None, NW, N, NE, E, SE, S, SW, W, Rotate };

SelHandle hit_selection_handle(const Selection& sel, int canvas_x, int canvas_y, double zoom);
void draw_selection_handles(const Cairo::RefPtr<Cairo::Context>& cr, const Selection& sel, int ox,
                            int oy, double zoom);

class SelectionXform {
public:
  bool active() const { return mode_ != Mode::None; }
  bool on_press(ToolHost* host, CanvasEvent event, double zoom);
  void on_motion(ToolHost* host, CanvasEvent event);
  void on_release(ToolHost* host);
  void on_cancel(ToolHost* host);

private:
  enum class Mode { None, Scale, Rotate };
  Mode mode_{Mode::None};
  SelHandle handle_{SelHandle::None};
  Rect start_rect_{};
  std::vector<std::uint8_t> orig_pixels_;
  std::vector<std::uint8_t> orig_coverage_;
  int orig_w_ = 0;
  int orig_h_ = 0;
  double start_angle_ = 0;
};

}  // namespace lundukepaint

#endif
