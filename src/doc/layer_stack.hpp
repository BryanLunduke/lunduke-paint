// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_LAYER_STACK_HPP
#define LUNDUKEPAINT_DOC_LAYER_STACK_HPP

#include "doc/layer.hpp"
#include "io/ora.hpp"

#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

class LayerStack {
public:
  LayerStack() = default;

  void reset(int width, int height, Color fill, const std::string& layer_name);

  int count() const { return static_cast<int>(layers_.size()); }
  int active_index() const { return active_; }
  void set_active_index(int index);

  int width() const { return width_; }
  int height() const { return height_; }

  Layer& active_layer();
  const Layer& active_layer() const;

  Layer& at(int index);
  const Layer& at(int index) const;

  Layer& tool_layer();
  const Layer& tool_layer() const;
  void clear_tool_layer();
  void copy_active_to_tool();

  // Copies the tree (or clears it). Used so a moved Document does not dangle.
  void set_ora_stack(const OraNode* node);
  bool has_ora_stack() const { return has_ora_stack_; }

  void replace_active(int width, int height, const std::uint8_t* rgba, int stride);
  void resize_scratch(int width, int height);

  // Insert layer at index (0 = bottom). Takes ownership. Returns index.
  int insert(int index, std::unique_ptr<Layer> layer);
  std::unique_ptr<Layer> take(int index);
  void replace_at(int index, std::unique_ptr<Layer> layer);
  void move_layer(int from, int to);

  std::string next_layer_name() const;

  // Composite visible user layers into dest (view.w × view.h RGBA).
  // If tool_index >= 0, that user layer is replaced by tool_override pixels
  // (same blend/opacity/visibility as the user layer).
  void composite_rect(std::uint8_t* dest, int dest_stride, Rect view,
                      const Layer* tool_override = nullptr, int tool_index = -1,
                      int skip_index = -1) const;
  Color composite_pixel(int x, int y, const Layer* tool_override = nullptr,
                        int tool_index = -1, int skip_index = -1) const;

  // Merge layer[index] onto layer[index-1] using the upper layer's blend/opacity.
  bool merge_down(int index);
  // Composite all visible layers into one; discard hidden layers.
  void flatten_visible();

  // Replace the entire user stack (used by undo of flatten / canvas ops).
  void replace_stack(int width, int height, std::vector<std::unique_ptr<Layer>> layers,
                     int active_index);

private:
  const Layer* display_layer(int index, const Layer* tool_override, int tool_index) const;
  void composite_node(const OraNode& node, std::uint8_t* dest, int dest_stride, Rect view,
                      const Layer* tool_override, int tool_index, int skip_index) const;

  std::vector<std::unique_ptr<Layer>> layers_;
  std::unique_ptr<Layer> tool_layer_;
  OraNode ora_stack_{};
  bool has_ora_stack_ = false;
  int active_ = 0;
  int width_ = 0;
  int height_ = 0;
  int name_serial_ = 1;
};

}  // namespace lundukepaint

#endif
