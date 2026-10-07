// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_LAYER_HPP
#define LUNDUKEPAINT_DOC_LAYER_HPP

#include "raster/blend.hpp"
#include "raster/types.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

class Layer;

struct LayerSnapshot {
  std::string name;
  bool visible = true;
  bool locked = false;
  float opacity = 1.0f;
  BlendMode blend = BlendMode::Normal;
  int offset_x = 0;
  int offset_y = 0;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> pixels;
};

LayerSnapshot snapshot_layer(const Layer& layer);
LayerSnapshot snapshot_layer_props(const Layer& layer);
std::unique_ptr<Layer> layer_from_snapshot(const LayerSnapshot& snap);

class Layer {
public:
  Layer(int width, int height, Color fill, std::string name);

  const std::string& name() const { return name_; }
  void set_name(std::string name) { name_ = std::move(name); }

  bool visible() const { return visible_; }
  void set_visible(bool v) { visible_ = v; }

  bool locked() const { return locked_; }
  void set_locked(bool v) { locked_ = v; }

  float opacity() const { return opacity_; }
  void set_opacity(float v);

  BlendMode blend() const { return blend_; }
  void set_blend(BlendMode v) { blend_ = v; }

  int offset_x() const { return offset_x_; }
  int offset_y() const { return offset_y_; }
  void set_offset(int x, int y);

  int width() const { return width_; }
  int height() const { return height_; }
  int stride() const { return stride_; }

  std::uint8_t* pixels() { return pixels_.data(); }
  const std::uint8_t* pixels() const { return pixels_.data(); }

  Color pixel(int x, int y) const;
  void set_pixel(int x, int y, Color color);

  void fill(Color color);
  void fill_rect(Rect rect, Color color);
  void clear_transparent();
  void set_pixels(int width, int height, const std::uint8_t* rgba, int stride);

  void copy_rect_from(const Layer& src, Rect rect);
  void write_rect(Rect rect, const std::uint8_t* rgba);
  void read_rect(Rect rect, std::uint8_t* rgba) const;

  void copy_from(const Layer& src);
  std::unique_ptr<Layer> clone() const;

  void invalidate_thumbnail();
  // Bumps whenever pixels change. Autosave reuses an encoded PNG while this matches.
  std::uint64_t revision() const { return revision_; }
  // Stable for the life of this layer object. Paired with revision() as a PNG cache key.
  std::uint64_t identity() const { return identity_; }
  const std::uint8_t* thumbnail() const;
  int thumbnail_width() const { return kThumbWidth; }
  int thumbnail_height() const { return kThumbHeight; }

private:
  void ensure_thumbnail() const;

  std::string name_;
  bool visible_ = true;
  bool locked_ = false;
  float opacity_ = 1.0f;
  BlendMode blend_ = BlendMode::Normal;
  int offset_x_ = 0;
  int offset_y_ = 0;
  int width_ = 0;
  int height_ = 0;
  int stride_ = 0;
  void note_pixels();

  std::vector<std::uint8_t> pixels_;
  mutable std::vector<std::uint8_t> thumb_;
  mutable bool thumb_valid_ = false;
  std::uint64_t revision_ = 1;
  std::uint64_t identity_ = 0;
};

inline void shift_to_layer(const Layer& layer, int& x, int& y) {
  x -= layer.offset_x();
  y -= layer.offset_y();
}

inline void shift_to_layer(const Layer& layer, double& x, double& y) {
  x -= static_cast<double>(layer.offset_x());
  y -= static_cast<double>(layer.offset_y());
}

// Stroke dirty rects are in layer-buffer space. Canvas invalidation is document space.
inline Rect layer_dirty_to_canvas(const Layer& layer, Rect dirty) {
  if (!dirty.empty()) {
    dirty.x += layer.offset_x();
    dirty.y += layer.offset_y();
  }
  return dirty;
}

}  // namespace lundukepaint

#endif
