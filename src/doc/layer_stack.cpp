// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/layer_stack.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace lundukepaint {

void LayerStack::reset(int width, int height, Color fill, const std::string& layer_name) {
  width_ = width;
  height_ = height;
  layers_.clear();
  layers_.push_back(std::make_unique<Layer>(width, height, fill, layer_name));
  tool_layer_ = std::make_unique<Layer>(width, height, Color::transparent(), "Tool");
  selection_layer_ = std::make_unique<Layer>(width, height, Color::transparent(), "Selection");
  active_ = 0;
  name_serial_ = 1;
}

void LayerStack::set_active_index(int index) {
  if (index >= 0 && index < count()) {
    active_ = index;
  }
}

Layer& LayerStack::active_layer() {
  return at(active_);
}

const Layer& LayerStack::active_layer() const {
  return at(active_);
}

Layer& LayerStack::at(int index) {
  if (index < 0 || index >= count()) {
    throw std::out_of_range("layer index");
  }
  return *layers_[static_cast<std::size_t>(index)];
}

const Layer& LayerStack::at(int index) const {
  if (index < 0 || index >= count()) {
    throw std::out_of_range("layer index");
  }
  return *layers_[static_cast<std::size_t>(index)];
}

Layer& LayerStack::tool_layer() {
  if (!tool_layer_) {
    tool_layer_ = std::make_unique<Layer>(width_, height_, Color::transparent(), "Tool");
  }
  return *tool_layer_;
}

const Layer& LayerStack::tool_layer() const {
  return *tool_layer_;
}

void LayerStack::clear_tool_layer() {
  if (tool_layer_) {
    tool_layer_->clear_transparent();
  }
}

void LayerStack::copy_active_to_tool() {
  tool_layer().copy_from(active_layer());
}

Layer& LayerStack::selection_layer() {
  if (!selection_layer_) {
    selection_layer_ = std::make_unique<Layer>(width_, height_, Color::transparent(), "Selection");
  }
  return *selection_layer_;
}

const Layer& LayerStack::selection_layer() const {
  return *selection_layer_;
}

void LayerStack::clear_selection_layer() {
  if (selection_layer_) {
    selection_layer_->clear_transparent();
  }
}

void LayerStack::replace_active(int width, int height, const std::uint8_t* rgba, int stride) {
  width_ = width;
  height_ = height;
  active_layer().set_pixels(width, height, rgba, stride);
  for (int i = 0; i < count(); ++i) {
    if (i == active_) {
      continue;
    }
    Layer& layer = at(i);
    if (layer.width() != width || layer.height() != height) {
      auto resized = std::make_unique<Layer>(width, height, Color::transparent(), layer.name());
      resized->set_visible(layer.visible());
      resized->set_locked(layer.locked());
      resized->set_opacity(layer.opacity());
      resized->set_blend(layer.blend());
      resized->set_offset(layer.offset_x(), layer.offset_y());
      const int cw = std::min(width, layer.width());
      const int ch = std::min(height, layer.height());
      if (cw > 0 && ch > 0) {
        for (int y = 0; y < ch; ++y) {
          std::memcpy(resized->pixels() + static_cast<std::size_t>(y) * resized->stride(),
                      layer.pixels() + static_cast<std::size_t>(y) * layer.stride(),
                      static_cast<std::size_t>(cw) * 4);
        }
      }
      layers_[static_cast<std::size_t>(i)] = std::move(resized);
    }
  }
  resize_scratch(width, height);
}

void LayerStack::resize_scratch(int width, int height) {
  width_ = width;
  height_ = height;
  tool_layer_ = std::make_unique<Layer>(width, height, Color::transparent(), "Tool");
  selection_layer_ = std::make_unique<Layer>(width, height, Color::transparent(), "Selection");
}

int LayerStack::insert(int index, std::unique_ptr<Layer> layer) {
  if (!layer) {
    return active_;
  }
  if (index < 0) {
    index = 0;
  }
  if (index > count()) {
    index = count();
  }
  layers_.insert(layers_.begin() + index, std::move(layer));
  if (active_ >= index) {
    ++active_;
  }
  return index;
}

std::unique_ptr<Layer> LayerStack::take(int index) {
  if (index < 0 || index >= count() || count() <= 1) {
    return nullptr;
  }
  auto taken = std::move(layers_[static_cast<std::size_t>(index)]);
  layers_.erase(layers_.begin() + index);
  if (active_ > index) {
    --active_;
  } else if (active_ >= count()) {
    active_ = count() - 1;
  }
  return taken;
}

void LayerStack::replace_at(int index, std::unique_ptr<Layer> layer) {
  if (index < 0 || index >= count() || !layer) {
    return;
  }
  layers_[static_cast<std::size_t>(index)] = std::move(layer);
}

void LayerStack::move_layer(int from, int to) {
  if (from < 0 || to < 0 || from >= count() || to >= count() || from == to) {
    return;
  }
  auto layer = std::move(layers_[static_cast<std::size_t>(from)]);
  layers_.erase(layers_.begin() + from);
  layers_.insert(layers_.begin() + to, std::move(layer));
  if (active_ == from) {
    active_ = to;
  } else if (from < active_ && to >= active_) {
    --active_;
  } else if (from > active_ && to <= active_) {
    ++active_;
  }
}

std::string LayerStack::next_layer_name() const {
  int n = name_serial_;
  for (;;) {
    std::string name = "Layer " + std::to_string(n);
    bool used = false;
    for (const auto& layer : layers_) {
      if (layer && layer->name() == name) {
        used = true;
        break;
      }
    }
    if (!used) {
      return name;
    }
    ++n;
  }
}

const Layer* LayerStack::display_layer(int index, const Layer* tool_override, int tool_index) const {
  if (tool_override != nullptr && index == tool_index) {
    return tool_override;
  }
  return &at(index);
}

void LayerStack::composite_rect(std::uint8_t* dest, int dest_stride, Rect view,
                                const Layer* tool_override, int tool_index) const {
  if (dest == nullptr || view.empty()) {
    return;
  }
  for (int y = 0; y < view.h; ++y) {
    std::memset(dest + static_cast<std::size_t>(y) * dest_stride, 0,
                static_cast<std::size_t>(view.w) * 4);
  }
  for (int i = 0; i < count(); ++i) {
    const Layer& meta = at(i);
    if (!meta.visible()) {
      continue;
    }
    const Layer* src = display_layer(i, tool_override, tool_index);
    blend_layer_rect(dest, view.w, view.h, dest_stride, src->pixels(), src->width(), src->height(),
                     src->stride(), meta.offset_x(), meta.offset_y(), view, meta.blend(),
                     meta.opacity());
  }
}

Color LayerStack::composite_pixel(int x, int y, const Layer* tool_override, int tool_index,
                                  int skip_index) const {
  std::uint8_t dest[4] = {0, 0, 0, 0};
  for (int i = 0; i < count(); ++i) {
    if (i == skip_index) {
      continue;
    }
    const Layer& meta = at(i);
    if (!meta.visible()) {
      continue;
    }
    const Layer* src = display_layer(i, tool_override, tool_index);
    const int sx = x - meta.offset_x();
    const int sy = y - meta.offset_y();
    if (sx < 0 || sy < 0 || sx >= src->width() || sy >= src->height()) {
      continue;
    }
    const std::uint8_t* p = src->pixels() + static_cast<std::size_t>(sy) * src->stride() +
                            static_cast<std::size_t>(sx) * 4;
    blend_pixel(dest, p, meta.blend(), meta.opacity());
  }
  return {dest[0], dest[1], dest[2], dest[3]};
}

bool LayerStack::merge_down(int index) {
  if (index <= 0 || index >= count()) {
    return false;
  }
  Layer& upper = at(index);
  Layer& lower = at(index - 1);
  std::vector<std::uint8_t> dest(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) *
                                 4);
  // Start with the lower layer as dest, then blend the upper onto it.
  for (int y = 0; y < height_; ++y) {
    std::memcpy(dest.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) * 4,
                lower.pixels() + static_cast<std::size_t>(y) * lower.stride(),
                static_cast<std::size_t>(std::min(width_, lower.width())) * 4);
  }
  blend_layer_rect(dest.data(), width_, height_, width_ * 4, upper.pixels(), upper.width(),
                   upper.height(), upper.stride(), upper.offset_x(), upper.offset_y(),
                   Rect{0, 0, width_, height_}, upper.blend(), upper.opacity());
  lower.set_pixels(width_, height_, dest.data(), width_ * 4);
  lower.set_offset(0, 0);
  take(index);
  active_ = index - 1;
  return true;
}

void LayerStack::flatten_visible() {
  std::vector<std::uint8_t> dest(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_) *
                                 4, 0);
  composite_rect(dest.data(), width_ * 4, Rect{0, 0, width_, height_});
  layers_.clear();
  auto flat = std::make_unique<Layer>(width_, height_, Color::transparent(), "Background");
  flat->set_pixels(width_, height_, dest.data(), width_ * 4);
  layers_.push_back(std::move(flat));
  active_ = 0;
}

void LayerStack::replace_stack(int width, int height, std::vector<std::unique_ptr<Layer>> layers,
                               int active_index) {
  width_ = width;
  height_ = height;
  layers_ = std::move(layers);
  if (layers_.empty()) {
    layers_.push_back(std::make_unique<Layer>(width_, height_, Color::white(), "Background"));
  }
  if (active_index < 0) {
    active_index = 0;
  }
  if (active_index >= count()) {
    active_index = count() - 1;
  }
  active_ = active_index;
  resize_scratch(width_, height_);
}

}  // namespace lundukepaint
