// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/layer.hpp"

#include "io/ora.hpp"

#include <algorithm>
#include <cstring>

namespace lundukepaint {
namespace {
std::uint64_t next_layer_identity = 1;
}
Layer::Layer(int width, int height, Color fill, std::string name)
    : name_(std::move(name)), width_(width), height_(height), stride_(width * 4),
      identity_(next_layer_identity++) {
  if (width_ < 1) {
    width_ = 1;
  }
  if (height_ < 1) {
    height_ = 1;
  }
  stride_ = width_ * 4;
  pixels_.assign(static_cast<std::size_t>(stride_) * static_cast<std::size_t>(height_), 0);
  this->fill(fill);
}

Layer::~Layer() {
  forget_layer_png(identity_);
}

void Layer::set_opacity(float v) {
  if (v < 0.0f) {
    v = 0.0f;
  }
  if (v > 1.0f) {
    v = 1.0f;
  }
  opacity_ = v;
}

void Layer::set_offset(int x, int y) {
  offset_x_ = x;
  offset_y_ = y;
}

void Layer::invalidate_thumbnail() {
  thumb_valid_ = false;
}

void Layer::note_pixels() {
  ++revision_;
  thumb_valid_ = false;
}

Color Layer::pixel(int x, int y) const {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    return Color::transparent();
  }
  const std::uint8_t* p = pixels_.data() + static_cast<std::size_t>(y) * stride_ +
                          static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

void Layer::set_pixel(int x, int y, Color color) {
  if (x < 0 || y < 0 || x >= width_ || y >= height_) {
    return;
  }
  std::uint8_t* p = pixels_.data() + static_cast<std::size_t>(y) * stride_ +
                    static_cast<std::size_t>(x) * 4;
  p[0] = color.r;
  p[1] = color.g;
  p[2] = color.b;
  p[3] = color.a;
  note_pixels();
}

void Layer::fill(Color color) {
  for (int y = 0; y < height_; ++y) {
    std::uint8_t* row = pixels_.data() + static_cast<std::size_t>(y) * stride_;
    for (int x = 0; x < width_; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      p[0] = color.r;
      p[1] = color.g;
      p[2] = color.b;
      p[3] = color.a;
    }
  }
  note_pixels();
}

void Layer::clear_transparent() {
  std::fill(pixels_.begin(), pixels_.end(), static_cast<std::uint8_t>(0));
  note_pixels();
}

void Layer::fill_rect(Rect rect, Color color) {
  rect = rect_intersect(rect, Rect{0, 0, width_, height_});
  if (rect.empty()) {
    return;
  }
  for (int y = rect.y; y < rect.y2(); ++y) {
    std::uint8_t* row = pixels_.data() + static_cast<std::size_t>(y) * stride_;
    for (int x = rect.x; x < rect.x2(); ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      p[0] = color.r;
      p[1] = color.g;
      p[2] = color.b;
      p[3] = color.a;
    }
  }
  note_pixels();
}

void Layer::set_pixels(int width, int height, const std::uint8_t* rgba, int stride) {
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }
  width_ = width;
  height_ = height;
  stride_ = width_ * 4;
  pixels_.assign(static_cast<std::size_t>(stride_) * static_cast<std::size_t>(height_), 0);
  if (rgba == nullptr) {
    invalidate_thumbnail();
    return;
  }
  const int row_bytes = width_ * 4;
  for (int y = 0; y < height_; ++y) {
    std::memcpy(pixels_.data() + static_cast<std::size_t>(y) * stride_,
                rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                static_cast<std::size_t>(row_bytes));
  }
  note_pixels();
}

void Layer::copy_rect_from(const Layer& src, Rect rect) {
  rect = rect_intersect(rect, Rect{0, 0, width_, height_});
  rect = rect_intersect(rect, Rect{0, 0, src.width_, src.height_});
  if (rect.empty()) {
    return;
  }
  for (int y = 0; y < rect.h; ++y) {
    const std::uint8_t* s = src.pixels_.data() +
                            static_cast<std::size_t>(rect.y + y) * src.stride_ +
                            static_cast<std::size_t>(rect.x) * 4;
    std::uint8_t* d = pixels_.data() + static_cast<std::size_t>(rect.y + y) * stride_ +
                      static_cast<std::size_t>(rect.x) * 4;
    std::memcpy(d, s, static_cast<std::size_t>(rect.w) * 4);
  }
  note_pixels();
}

void Layer::write_rect(Rect rect, const std::uint8_t* rgba) {
  rect = rect_intersect(rect, Rect{0, 0, width_, height_});
  if (rect.empty() || rgba == nullptr) {
    return;
  }
  for (int y = 0; y < rect.h; ++y) {
    std::uint8_t* d = pixels_.data() + static_cast<std::size_t>(rect.y + y) * stride_ +
                      static_cast<std::size_t>(rect.x) * 4;
    const std::uint8_t* s = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(rect.w) * 4;
    std::memcpy(d, s, static_cast<std::size_t>(rect.w) * 4);
  }
  note_pixels();
}

void Layer::read_rect(Rect rect, std::uint8_t* rgba) const {
  rect = rect_intersect(rect, Rect{0, 0, width_, height_});
  if (rect.empty() || rgba == nullptr) {
    return;
  }
  for (int y = 0; y < rect.h; ++y) {
    const std::uint8_t* s = pixels_.data() + static_cast<std::size_t>(rect.y + y) * stride_ +
                            static_cast<std::size_t>(rect.x) * 4;
    std::uint8_t* d = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(rect.w) * 4;
    std::memcpy(d, s, static_cast<std::size_t>(rect.w) * 4);
  }
}

void Layer::copy_from(const Layer& src) {
  if (src.width_ != width_ || src.height_ != height_) {
    return;
  }
  pixels_ = src.pixels_;
  note_pixels();
}

std::unique_ptr<Layer> Layer::clone() const {
  auto copy = std::make_unique<Layer>(width_, height_, Color::transparent(), name_);
  copy->visible_ = visible_;
  copy->locked_ = locked_;
  copy->opacity_ = opacity_;
  copy->blend_ = blend_;
  copy->offset_x_ = offset_x_;
  copy->offset_y_ = offset_y_;
  copy->pixels_ = pixels_;
  copy->thumb_valid_ = false;
  return copy;
}

void Layer::ensure_thumbnail() const {
  if (thumb_valid_ && static_cast<int>(thumb_.size()) == kThumbWidth * kThumbHeight * 4) {
    return;
  }
  thumb_.assign(static_cast<std::size_t>(kThumbWidth) * static_cast<std::size_t>(kThumbHeight) * 4,
                0);
  if (width_ < 1 || height_ < 1) {
    thumb_valid_ = true;
    return;
  }
  for (int y = 0; y < kThumbHeight; ++y) {
    const int sy = std::min(height_ - 1, y * height_ / kThumbHeight);
    std::uint8_t* drow = thumb_.data() + static_cast<std::size_t>(y) * kThumbWidth * 4;
    const std::uint8_t* srow = pixels_.data() + static_cast<std::size_t>(sy) * stride_;
    for (int x = 0; x < kThumbWidth; ++x) {
      const int sx = std::min(width_ - 1, x * width_ / kThumbWidth);
      const std::uint8_t* s = srow + static_cast<std::size_t>(sx) * 4;
      std::uint8_t* d = drow + static_cast<std::size_t>(x) * 4;
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
      d[3] = s[3];
    }
  }
  thumb_valid_ = true;
}

const std::uint8_t* Layer::thumbnail() const {
  ensure_thumbnail();
  return thumb_.data();
}

LayerSnapshot snapshot_layer(const Layer& layer) {
  LayerSnapshot snap;
  snap.name = layer.name();
  snap.visible = layer.visible();
  snap.locked = layer.locked();
  snap.opacity = layer.opacity();
  snap.blend = layer.blend();
  snap.offset_x = layer.offset_x();
  snap.offset_y = layer.offset_y();
  snap.width = layer.width();
  snap.height = layer.height();
  snap.pixels.assign(static_cast<std::size_t>(layer.stride()) * static_cast<std::size_t>(layer.height()),
                     0);
  if (layer.stride() == layer.width() * 4) {
    std::memcpy(snap.pixels.data(), layer.pixels(), snap.pixels.size());
  } else {
    snap.pixels.assign(static_cast<std::size_t>(layer.width()) * static_cast<std::size_t>(layer.height()) *
                           4,
                       0);
    for (int y = 0; y < layer.height(); ++y) {
      std::memcpy(snap.pixels.data() +
                      static_cast<std::size_t>(y) * static_cast<std::size_t>(layer.width()) * 4,
                  layer.pixels() + static_cast<std::size_t>(y) * layer.stride(),
                  static_cast<std::size_t>(layer.width()) * 4);
    }
  }
  return snap;
}

LayerSnapshot snapshot_layer_props(const Layer& layer) {
  LayerSnapshot snap;
  snap.name = layer.name();
  snap.visible = layer.visible();
  snap.locked = layer.locked();
  snap.opacity = layer.opacity();
  snap.blend = layer.blend();
  snap.offset_x = layer.offset_x();
  snap.offset_y = layer.offset_y();
  snap.width = layer.width();
  snap.height = layer.height();
  return snap;
}

std::unique_ptr<Layer> layer_from_snapshot(const LayerSnapshot& snap) {

  auto layer = std::make_unique<Layer>(std::max(1, snap.width), std::max(1, snap.height),
                                       Color::transparent(), snap.name);
  layer->set_visible(snap.visible);
  layer->set_locked(snap.locked);
  layer->set_opacity(snap.opacity);
  layer->set_blend(snap.blend);
  layer->set_offset(snap.offset_x, snap.offset_y);
  if (!snap.pixels.empty()) {
    layer->set_pixels(snap.width, snap.height, snap.pixels.data(), snap.width * 4);
  }
  return layer;
}

}  // namespace lundukepaint
