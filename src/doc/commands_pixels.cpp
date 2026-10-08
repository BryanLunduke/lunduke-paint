// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_pixels.hpp"

#include "doc/document.hpp"
#include "doc/layer.hpp"

#include <zlib.h>

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

void append_side(bool& solid, Color& color, std::uint32_t& off, std::uint32_t& len,
                 std::vector<std::uint8_t>& raw, const std::uint8_t* src, int w, int h, int stride) {
  Color flat;
  if (tile_is_solid(src, w, h, stride, flat)) {
    solid = true;
    color = flat;
    off = 0;
    len = 0;
    return;
  }
  solid = false;
  const std::size_t n = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4;
  off = static_cast<std::uint32_t>(raw.size());
  len = static_cast<std::uint32_t>(n);
  const std::size_t at = raw.size();
  raw.resize(at + n);
  for (int y = 0; y < h; ++y) {
    std::memcpy(raw.data() + at + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4,
                src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride),
                static_cast<std::size_t>(w) * 4);
  }
}

}  // namespace

void PixelPatchCommand::pack_pixels(const std::vector<std::uint8_t>& raw, PackedPixels& out) {
  out = {};
  out.raw_size = static_cast<std::uint32_t>(raw.size());
  if (raw.empty()) {
    return;
  }
  if (raw.size() > 64) {
    const uLongf bound = compressBound(static_cast<uLong>(raw.size()));
    out.data.resize(bound);
    uLongf dest = bound;
    const int rc = compress2(out.data.data(), &dest, raw.data(), static_cast<uLong>(raw.size()), 1);
    if (rc == Z_OK && static_cast<std::size_t>(dest) + 16 < raw.size()) {
      out.data.resize(dest);
      out.compressed = true;
      return;
    }
  }
  out.compressed = false;
  out.data = raw;
}

bool PixelPatchCommand::unpack_pixels(const PackedPixels& in, std::vector<std::uint8_t>& raw) {
  raw.clear();
  if (in.raw_size == 0) {
    return true;
  }
  raw.resize(in.raw_size);
  if (!in.compressed) {
    if (in.data.size() != in.raw_size) {
      return false;
    }
    std::memcpy(raw.data(), in.data.data(), in.raw_size);
    return true;
  }
  uLongf dest = in.raw_size;
  if (uncompress(raw.data(), &dest, in.data.data(), static_cast<uLong>(in.data.size())) != Z_OK) {
    return false;
  }
  return dest == in.raw_size;
}

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
  std::vector<std::uint8_t> before_raw;
  std::vector<std::uint8_t> after_raw;
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
      append_side(tile.before_solid, tile.before_color, tile.before_off, tile.before_len, before_raw,
                  a, tw, th, before.stride());
      append_side(tile.after_solid, tile.after_color, tile.after_off, tile.after_len, after_raw, b,
                  tw, th, after.stride());
      cmd->tiles_.push_back(std::move(tile));
    }
  }
  pack_pixels(before_raw, cmd->before_packed_);
  pack_pixels(after_raw, cmd->after_packed_);

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
  bytes += before_packed_.bytes();
  bytes += after_packed_.bytes();
  bytes += tiles_.size() * 48;
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
    std::vector<std::uint8_t> after;
    if (unpack_pixels(after_packed_, after)) {
      for (const Tile& tile : tiles_) {
        if (tile.after_solid) {
          write_solid(layer, tile.x, tile.y, tile.w, tile.h, tile.after_color);
        } else if (static_cast<std::size_t>(tile.after_off) + tile.after_len <= after.size()) {
          write_tile(layer, tile.x, tile.y, tile.w, tile.h, after.data() + tile.after_off);
        }
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
    std::vector<std::uint8_t> before;
    if (unpack_pixels(before_packed_, before)) {
      for (const Tile& tile : tiles_) {
        if (tile.before_solid) {
          write_solid(layer, tile.x, tile.y, tile.w, tile.h, tile.before_color);
        } else if (static_cast<std::size_t>(tile.before_off) + tile.before_len <= before.size()) {
          write_tile(layer, tile.x, tile.y, tile.w, tile.h, before.data() + tile.before_off);
        }
      }
    }
  }
  if (has_selection_) {
    document.selection().restore(selection_before_);
  }
}

}  // namespace lundukepaint
