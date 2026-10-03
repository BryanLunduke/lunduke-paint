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

void write_tile(Layer& layer, int x, int y, int w, int h, const std::uint8_t* packed) {
  for (int row = 0; row < h; ++row) {
    const Rect line{x, y + row, w, 1};
    layer.write_rect(line, packed + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4);
  }
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
      tile.before = copy_tile(a, tw, th, before.stride());
      tile.after = copy_tile(b, tw, th, after.stride());
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

void PixelPatchCommand::apply(Document& document) {
  Layer& layer = document.layers().at(layer_index_);
  for (const Tile& tile : tiles_) {
    write_tile(layer, tile.x, tile.y, tile.w, tile.h, tile.after.data());
  }
}

void PixelPatchCommand::undo(Document& document) {
  Layer& layer = document.layers().at(layer_index_);
  for (const Tile& tile : tiles_) {
    write_tile(layer, tile.x, tile.y, tile.w, tile.h, tile.before.data());
  }
}

}  // namespace lundukepaint
