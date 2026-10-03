// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/layer_stack.hpp"

#include <algorithm>
#include <cmath>
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
  const Layer& src = active_layer();
  if (!tool_layer_ || tool_layer_->width() != src.width() || tool_layer_->height() != src.height()) {
    tool_layer_ = std::make_unique<Layer>(src.width(), src.height(), Color::transparent(), "Tool");
  }
  tool_layer_->set_offset(src.offset_x(), src.offset_y());
  tool_layer_->copy_from(src);
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

namespace {

void blend_one(std::uint8_t* dest, int width, int height, const Layer& layer) {
  if (!layer.visible()) {
    return;
  }
  blend_layer_rect(dest, width, height, width * 4, layer.pixels(), layer.width(), layer.height(),
                   layer.stride(), layer.offset_x(), layer.offset_y(), Rect{0, 0, width, height},
                   layer.blend(), layer.opacity());
}

int forward_coverage(int src_a, int dest_a) {
  return src_a + (dest_a * (255 - src_a) + 127) / 255;
}

// Pixels of a Normal, fully-opaque layer which, placed over `below`, reproduce `full`.
void uncomposite_normal(const std::uint8_t* below, const std::uint8_t* full, std::uint8_t* out) {
  if (below[3] == 0) {
    std::memcpy(out, full, 4);
    return;
  }
  if (std::memcmp(below, full, 4) == 0) {
    out[0] = out[1] = out[2] = out[3] = 0;
    return;
  }
  const int da = below[3];
  const int oa = full[3];
  int lo = 0;
  int hi = 255;
  while (lo < hi) {
    const int mid = (lo + hi) / 2;
    if (forward_coverage(mid, da) < oa) {
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  int guess = lo;
  int guess_err = std::abs(forward_coverage(lo, da) - oa);
  if (lo > 0) {
    const int err = std::abs(forward_coverage(lo - 1, da) - oa);
    if (err < guess_err) {
      guess_err = err;
      guess = lo - 1;
    }
  }
  if (lo < 255) {
    const int err = std::abs(forward_coverage(lo + 1, da) - oa);
    if (err < guess_err) {
      guess = lo + 1;
    }
  }

  auto score = [&](int src_a, int sr, int sg, int sb, std::uint8_t* dest) {
    dest[0] = static_cast<std::uint8_t>(std::clamp(sr, 0, 255));
    dest[1] = static_cast<std::uint8_t>(std::clamp(sg, 0, 255));
    dest[2] = static_cast<std::uint8_t>(std::clamp(sb, 0, 255));
    dest[3] = static_cast<std::uint8_t>(std::clamp(src_a, 0, 255));
    std::uint8_t tmp[4] = {below[0], below[1], below[2], below[3]};
    blend_pixel(tmp, dest, BlendMode::Normal, 1.0f);
    int err = 0;
    for (int i = 0; i < 4; ++i) {
      err += std::abs(static_cast<int>(tmp[i]) - static_cast<int>(full[i]));
    }
    return err;
  };

  std::uint8_t best[4] = {0, 0, 0, 0};
  int best_err = 1000000;
  for (int delta = -2; delta <= 2; ++delta) {
    const int src_a = std::clamp(guess + delta, 0, 255);
    int sr = 0;
    int sg = 0;
    int sb = 0;
    if (src_a > 0) {
      const int pred_a = std::max(1, forward_coverage(src_a, da));
      const int inv = 255 - src_a;
      const int rgb_full[3] = {full[0], full[1], full[2]};
      const int rgb_below[3] = {below[0], below[1], below[2]};
      int solved[3] = {0, 0, 0};
      for (int c = 0; c < 3; ++c) {
        const int term = rgb_below[c] * da * inv / 255;
        const int num = rgb_full[c] * pred_a - term - pred_a / 2;
        int v = 0;
        if (num >= 0) {
          v = (num + src_a / 2) / src_a;
        } else {
          v = -(((-num) + src_a / 2) / src_a);
        }
        int best_v = std::clamp(v, 0, 255);
        int best_c = 1000000;
        for (int dv = -3; dv <= 3; ++dv) {
          const int cand = std::clamp(v + dv, 0, 255);
          const int pred = (cand * src_a + term + pred_a / 2) / pred_a;
          const int err = std::abs(pred - rgb_full[c]);
          if (err < best_c) {
            best_c = err;
            best_v = cand;
          }
        }
        solved[c] = best_v;
      }
      sr = solved[0];
      sg = solved[1];
      sb = solved[2];
    }
    std::uint8_t cand[4];
    const int err = score(src_a, sr, sg, sb, cand);
    if (err < best_err) {
      best_err = err;
      std::memcpy(best, cand, 4);
      if (err == 0) {
        break;
      }
    }
  }
  std::memcpy(out, best, 4);
}

}  // namespace

bool LayerStack::merge_down(int index) {
  if (index <= 0 || index >= count()) {
    return false;
  }
  const int width = width_;
  const int height = height_;
  const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  std::vector<std::uint8_t> below(pixels * 4, 0);
  for (int i = 0; i < index - 1; ++i) {
    blend_one(below.data(), width, height, at(i));
  }
  std::vector<std::uint8_t> full = below;
  const bool show = at(index - 1).visible() || at(index).visible();
  blend_one(full.data(), width, height, at(index - 1));
  blend_one(full.data(), width, height, at(index));

  std::vector<std::uint8_t> baked(pixels * 4, 0);
  for (std::size_t i = 0; i < pixels; ++i) {
    uncomposite_normal(below.data() + i * 4, full.data() + i * 4, baked.data() + i * 4);
  }

  Layer& lower = at(index - 1);
  lower.set_pixels(width, height, baked.data(), width * 4);
  lower.set_offset(0, 0);
  lower.set_opacity(1.0f);
  lower.set_blend(BlendMode::Normal);
  lower.set_visible(show);
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
