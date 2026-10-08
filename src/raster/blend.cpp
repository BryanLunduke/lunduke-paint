// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/blend.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace lundukepaint {
namespace {

inline std::uint8_t clamp_u8(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 255) {
    return 255;
  }
  return static_cast<std::uint8_t>(v);
}

inline int blend_channel(int src, int dst, BlendMode mode) {
  switch (mode) {
    case BlendMode::Multiply:
      return (src * dst + 127) / 255;
    case BlendMode::Screen:
      return src + dst - (src * dst + 127) / 255;
    case BlendMode::Overlay:
      if (dst < 128) {
        return (2 * src * dst + 127) / 255;
      }
      return 255 - (2 * (255 - src) * (255 - dst) + 127) / 255;
    case BlendMode::Darken:
      return std::min(src, dst);
    case BlendMode::Lighten:
      return std::max(src, dst);
    case BlendMode::Normal:
    default:
      return src;
  }
}

}  // namespace

const char* blend_mode_label(BlendMode mode) {
  switch (mode) {
    case BlendMode::Multiply:
      return "Multiply";
    case BlendMode::Screen:
      return "Screen";
    case BlendMode::Overlay:
      return "Overlay";
    case BlendMode::Darken:
      return "Darken";
    case BlendMode::Lighten:
      return "Lighten";
    case BlendMode::Normal:
    default:
      return "Normal";
  }
}

const char* blend_mode_ora_op(BlendMode mode) {
  switch (mode) {
    case BlendMode::Multiply:
      return "svg:multiply";
    case BlendMode::Screen:
      return "svg:screen";
    case BlendMode::Overlay:
      return "svg:overlay";
    case BlendMode::Darken:
      return "svg:darken";
    case BlendMode::Lighten:
      return "svg:lighten";
    case BlendMode::Normal:
    default:
      return "svg:src-over";
  }
}

BlendMode blend_mode_from_ora(const char* composite_op) {
  if (composite_op == nullptr) {
    return BlendMode::Normal;
  }
  const std::string op(composite_op);
  if (op == "svg:multiply") {
    return BlendMode::Multiply;
  }
  if (op == "svg:screen") {
    return BlendMode::Screen;
  }
  if (op == "svg:overlay") {
    return BlendMode::Overlay;
  }
  if (op == "svg:darken") {
    return BlendMode::Darken;
  }
  if (op == "svg:lighten") {
    return BlendMode::Lighten;
  }
  return BlendMode::Normal;
}

BlendMode blend_mode_from_index(int index) {
  if (index < 0 || index >= kBlendModeCount) {
    return BlendMode::Normal;
  }
  return static_cast<BlendMode>(index);
}

void blend_pixel(std::uint8_t* dest, const std::uint8_t* src, BlendMode mode, float opacity) {
  if (dest == nullptr || src == nullptr) {
    return;
  }
  if (opacity <= 0.0f) {
    return;
  }
  if (opacity > 1.0f) {
    opacity = 1.0f;
  }
  const int sa = static_cast<int>(static_cast<float>(src[3]) * opacity + 0.5f);
  if (sa <= 0) {
    return;
  }
  const int da = dest[3];
  if (da <= 0) {
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
    dest[3] = static_cast<std::uint8_t>(sa);
    return;
  }

  const int br = blend_channel(src[0], dest[0], mode);
  const int bg = blend_channel(src[1], dest[1], mode);
  const int bb = blend_channel(src[2], dest[2], mode);
  const int inv = 255 - sa;
  const int out_a = sa + (da * inv + 127) / 255;
  if (out_a <= 0) {
    dest[0] = dest[1] = dest[2] = dest[3] = 0;
    return;
  }
  const int out_r = (br * sa + dest[0] * da * inv / 255 + out_a / 2) / out_a;
  const int out_g = (bg * sa + dest[1] * da * inv / 255 + out_a / 2) / out_a;
  const int out_b = (bb * sa + dest[2] * da * inv / 255 + out_a / 2) / out_a;
  dest[0] = clamp_u8(out_r);
  dest[1] = clamp_u8(out_g);
  dest[2] = clamp_u8(out_b);
  dest[3] = clamp_u8(out_a);
}

void blend_layer_rect(std::uint8_t* dest, int dest_w, int dest_h, int dest_stride,
                      const std::uint8_t* src, int src_w, int src_h, int src_stride,
                      int offset_x, int offset_y, Rect view, BlendMode mode, float opacity) {
  if (dest == nullptr || src == nullptr || dest_w < 1 || dest_h < 1) {
    return;
  }
  const int vw = std::min(dest_w, view.w);
  const int vh = std::min(dest_h, view.h);
  if (vw < 1 || vh < 1) {
    return;
  }
  const bool replace = mode == BlendMode::Normal && opacity >= 0.999f;
  for (int y = 0; y < vh; ++y) {
    const int src_y = (view.y + y) - offset_y;
    if (src_y < 0 || src_y >= src_h) {
      continue;
    }
    std::uint8_t* drow = dest + static_cast<std::size_t>(y) * dest_stride;
    const std::uint8_t* srow = src + static_cast<std::size_t>(src_y) * src_stride;
    int x = 0;
    while (x < vw) {
      const int src_x = (view.x + x) - offset_x;
      if (src_x < 0 || src_x >= src_w) {
        ++x;
        continue;
      }
      const std::uint8_t* s = srow + static_cast<std::size_t>(src_x) * 4;
      if (replace && s[3] == 255) {
        int run = 1;
        while (x + run < vw) {
          const int sx = (view.x + x + run) - offset_x;
          if (sx < 0 || sx >= src_w) {
            break;
          }
          if (srow[static_cast<std::size_t>(sx) * 4 + 3] != 255) {
            break;
          }
          ++run;
        }
        std::memcpy(drow + static_cast<std::size_t>(x) * 4, s, static_cast<std::size_t>(run) * 4);
        x += run;
        continue;
      }
      blend_pixel(drow + static_cast<std::size_t>(x) * 4, s, mode, opacity);
      ++x;
    }
  }
}

}  // namespace lundukepaint
