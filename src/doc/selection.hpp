// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_SELECTION_HPP
#define LUNDUKEPAINT_DOC_SELECTION_HPP

#include "raster/types.hpp"

#include <cstdint>
#include <vector>

namespace lundukepaint {

class Layer;
class LayerStack;

// One selection per document (not per layer). Rect, ellipse, or lasso
// (optional 8-bit mask). Invert means "canvas minus the chosen region."
class Selection {
public:
  bool empty() const { return empty_; }
  bool inverted() const { return inverted_; }
  bool floating() const { return floating_; }

  bool transparent_move() const { return transparent_move_; }
  void set_transparent_move(bool enabled) { transparent_move_ = enabled; }

  bool copy_mode() const { return copy_mode_; }
  void set_copy_mode(bool enabled) { copy_mode_ = enabled; }

  // Chosen rectangle (the hole when inverted; the float dest when floating).
  Rect bounds() const;

  bool contains(int x, int y) const;

  void clear();
  void set_rect(Rect rect);
  void set_mask(Rect bounds, std::vector<std::uint8_t> mask);
  // Bumps when the outline geometry changes (marching ants rebuild from this).
  unsigned generation() const { return generation_; }

  bool has_mask() const { return !mask_.empty(); }
  const std::uint8_t* mask() const { return mask_.empty() ? nullptr : mask_.data(); }
  int mask_w() const { return mask_w_; }
  int mask_h() const { return mask_h_; }
  bool mask_at(int canvas_x, int canvas_y) const;
  void select_all(int width, int height);
  void invert(int width, int height);

  int float_x() const { return float_x_; }
  int float_y() const { return float_y_; }
  int float_w() const { return float_w_; }
  int float_h() const { return float_h_; }
  int origin_x() const { return origin_x_; }
  int origin_y() const { return origin_y_; }
  int origin_w() const { return origin_w_; }
  int origin_h() const { return origin_h_; }

  Rect float_rect() const;
  Rect origin_rect() const;
  Rect dirty_union() const;

  const std::uint8_t* float_pixels() const {
    return float_pixels_.empty() ? nullptr : float_pixels_.data();
  }
  // Local to the float. Empty means every float pixel is covered (a rectangle).
  // A non-empty buffer is 0 outside the lasso / wand / ellipse.
  const std::uint8_t* float_coverage() const {
    return float_coverage_.empty() ? nullptr : float_coverage_.data();
  }
  bool has_float_coverage() const { return !float_coverage_.empty(); }
  bool float_covers(int local_x, int local_y) const;
  Color float_pixel(int x, int y) const;

  // Layer the pixels were lifted from. -1 until lift / paste.
  int source_layer() const { return source_layer_; }
  void set_source_layer(int index) { source_layer_ = index; }

  // Copy the current rect from the layer into a floating buffer. Does not
  // modify the layer (the hole is previewed until commit). Origin stays in
  // canvas space. `source_index` is the layer the pixels must commit back to.
  bool lift(const Layer& layer, int source_index = -1);

  void set_float_pixels(int x, int y, int w, int h, std::vector<std::uint8_t> rgba);
  void transform_float(int x, int y, int w, int h, std::vector<std::uint8_t> rgba);
  void move_float(int x, int y);
  void drop_float();

private:
  bool empty_ = true;
  bool inverted_ = false;
  bool floating_ = false;
  bool transparent_move_ = false;
  bool copy_mode_ = false;
  Rect rect_{};
  int float_x_ = 0;
  int float_y_ = 0;
  int float_w_ = 0;
  int float_h_ = 0;
  int origin_x_ = 0;
  int origin_y_ = 0;
  int origin_w_ = 0;
  int origin_h_ = 0;
  std::vector<std::uint8_t> float_pixels_;
  std::vector<std::uint8_t> float_coverage_;
  std::vector<std::uint8_t> mask_;
  int mask_w_ = 0;
  int mask_h_ = 0;
  int source_layer_ = -1;
  unsigned generation_ = 0;

  void bump();
};

void clip_rect_to_selection(Layer& dest, const Layer& source, Rect rect, const Selection& sel);
void fill_selection(Layer& layer, const Selection& sel, Color color, Rect* dirty);
void copy_selection_rgba(const Layer& layer, const Selection& sel, int canvas_w, int canvas_h,
                         int& out_w, int& out_h, std::vector<std::uint8_t>& out);
void copy_merged_rgba(const LayerStack& layers, const Selection& sel, int canvas_w, int canvas_h,
                      int& out_w, int& out_h, std::vector<std::uint8_t>& out);
void blit_rgba(Layer& dest, int dx, int dy, const std::uint8_t* src, int sw, int sh, int sstride,
               bool skip_transparent, const std::uint8_t* coverage = nullptr);

// Composite `sel` onto a view-sized straight-RGBA buffer. `dest` row 0 is
// `view.y`. The origin hole is composited once with the source layer skipped,
// then the float is blended in bulk. Never samples the stack per pixel.
// Only pixels inside the float coverage are punched or blitted.
// `hole_rgba`, when set, is the straight composite of `origin_rect()` (the
// source layer already omitted) so a redraw can reuse it.
void paint_floating_selection(const LayerStack& layers, const Selection& sel, std::uint8_t* dest,
                              int dest_stride, Rect view, bool substitute_clear, Color hole_clear,
                              Color float_clear, const std::uint8_t* hole_rgba = nullptr,
                              int hole_stride = 0);

}  // namespace lundukepaint

#endif
