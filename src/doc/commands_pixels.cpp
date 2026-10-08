// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_pixels.hpp"

#include "doc/document.hpp"
#include "doc/layer.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace lundukepaint {
namespace {

bool tiles_equal(const std::uint8_t* a, const std::uint8_t* b, int w, int h, int stride_a,
                 int stride_b) {
  for (int y = 0; y < h; ++y) {
    if (std::memcmp(a + static_cast<std::size_t>(y) * stride_a,
                    b + static_cast<std::size_t>(y) * stride_b,
                    static_cast<std::size_t>(w) * 4) != 0) {
      return false;
    }
  }
  return true;
}

std::vector<std::uint8_t> copy_tile(const std::uint8_t* src, int w, int h, int stride) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
  for (int y = 0; y < h; ++y) {
    std::memcpy(out.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4,
                src + static_cast<std::size_t>(y) * stride,
                static_cast<std::size_t>(w) * 4);
  }
  return out;
}

bool tile_is_solid(const std::uint8_t* src, int w, int h, int stride, Color& color) {
  if (src == nullptr || w < 1 || h < 1) {
    return false;
  }
  color = Color{src[0], src[1], src[2], src[3]};
  for (int y = 0; y < h; ++y) {
    const std::uint8_t* row = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < w; ++x) {
      const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      if (p[0] != color.r || p[1] != color.g || p[2] != color.b || p[3] != color.a) {
        return false;
      }
    }
  }
  return true;
}

void write_tile(Layer& layer, int x, int y, int w, int h, const std::uint8_t* packed) {
  for (int row = 0; row < h; ++row) {
    const Rect line{x, y + row, w, 1};
    layer.write_rect(line, packed + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
  }
}

void write_solid(Layer& layer, int x, int y, int w, int h, Color color) {
  std::vector<std::uint8_t> row(static_cast<std::size_t>(w) * 4);
  for (int i = 0; i < w; ++i) {
    row[static_cast<std::size_t>(i) * 4] = color.r;
    row[static_cast<std::size_t>(i) * 4 + 1] = color.g;
    row[static_cast<std::size_t>(i) * 4 + 2] = color.b;
    row[static_cast<std::size_t>(i) * 4 + 3] = color.a;
  }
  for (int line = 0; line < h; ++line) {
    layer.write_rect(Rect{x, y + line, w, 1}, row.data());
  }
}

void store_side(bool& solid, Color& color, std::vector<std::uint8_t>& bytes, const std::uint8_t* src,
                int w, int h, int stride) {
  Color flat;
  if (tile_is_solid(src, w, h, stride, flat)) {
    solid = true;
    color = flat;
    bytes.clear();
    return;
  }
  solid = false;
  bytes = copy_tile(src, w, h, stride);
}

}  // namespace

std::unique_ptr<PixelPatchCommand> PixelPatchCommand::from_layers(const Layer& before,
                                                                  const Layer& after, Rect bounds,
                                                                  std::string name,
                                                                  int layer_index) {
  auto cmd = std::unique_ptr<PixelPatchCommand>(new PixelPatchCommand());
  cmd->name_ = std::move(name);
  cmd->layer_index_ = layer_index;
  bounds = rect_intersect(bounds, Rect{0, 0, before.width(), before.height()});
  bounds = rect_intersect(bounds, Rect{0, 0, after.width(), after.height()});
  cmd->bounds_ = bounds;
  if (bounds.empty()) {
    return cmd;
  }

  const int x1 = bounds.x2();
  const int y1 = bounds.y2();
  for (int ty = bounds.y; ty < y1; ty += kPatchTile) {
    for (int tx = bounds.x; tx < x1; tx += kPatchTile) {
      const int tw = std::min(kPatchTile, x1 - tx);
      const int th = std::min(kPatchTile, y1 - ty);
      const std::uint8_t* a = before.pixels() + static_cast<std::size_t>(ty) * before.stride() +
                              static_cast<std::size_t>(tx) * 4;
      const std::uint8_t* b = after.pixels() + static_cast<std::size_t>(ty) * after.stride() +
                              static_cast<std::size_t>(tx) * 4;
      if (tiles_equal(a, b, tw, th, before.stride(), after.stride())) {
        continue;
      }
      Tile tile;
      tile.x = tx;
      tile.y = ty;
      tile.w = tw;
      tile.h = th;
      store_side(tile.before_solid, tile.before_color, tile.before, a, tw, th, before.stride());
      store_side(tile.after_solid, tile.after_color, tile.after, b, tw, th, after.stride());
      cmd->tiles_.push_back(std::move(tile));
    }
  }

  if (cmd->tiles_.empty()) {
    cmd->bounds_ = {};
  } else {
    cmd->bounds_.x += before.offset_x();
    cmd->bounds_.y += before.offset_y();
  }
  return cmd;
}

void PixelPatchCommand::set_selection_change(SelectionState before, SelectionState after) {
  has_selection_ = true;
  selection_before_ = std::move(before);
  selection_after_ = std::move(after);
  auto cover = [](const SelectionState& state) {
    if (state.empty) {
      return Rect{};
    }
    if (state.floating) {
      return rect_union(Rect{state.origin_x, state.origin_y, state.origin_w, state.origin_h},
                        Rect{state.float_x, state.float_y, state.float_w, state.float_h});
    }
    return state.rect;
  };
  bounds_ = rect_union(bounds_, rect_union(cover(selection_before_), cover(selection_after_)));
}

std::size_t PixelPatchCommand::memory_bytes() const {
  std::size_t bytes = 64;
  for (const Tile& tile : tiles_) {
    bytes += 48;
    bytes += tile.before.size();
    bytes += tile.after.size();
  }
  auto add_state = [&](const SelectionState& state) {
    bytes += state.float_pixels.size();
    bytes += state.float_coverage.size();
    bytes += state.origin_coverage.size();
    bytes += state.mask.size();
  };
  if (has_selection_) {
    add_state(selection_before_);
    add_state(selection_after_);
  }
  return bytes;
}

void PixelPatchCommand::apply(Document& document) {
  if (layer_index_ >= 0 && layer_index_ < document.layers().count()) {
    Layer& layer = document.layers().at(layer_index_);
    for (const Tile& tile : tiles_) {
      if (tile.after_solid) {
        write_solid(layer, tile.x, tile.y, tile.w, tile.h, tile.after_color);
      } else {
        write_tile(layer, tile.x, tile.y, tile.w, tile.h, tile.after.data());
      }
    }
  }
  if (has_selection_) {
    document.selection().restore(selection_after_);
  }
}

void PixelPatchCommand::undo(Document& document) {
  if (layer_index_ >= 0 && layer_index_ < document.layers().count()) {
    Layer& layer = document.layers().at(layer_index_);
    for (const Tile& tile : tiles_) {
      if (tile.before_solid) {
        write_solid(layer, tile.x, tile.y, tile.w, tile.h, tile.before_color);
      } else {
        write_tile(layer, tile.x, tile.y, tile.w, tile.h, tile.before.data());
      }
    }
  }
  if (has_selection_) {
    document.selection().restore(selection_before_);
  }
}

}  // namespace lundukepaint
