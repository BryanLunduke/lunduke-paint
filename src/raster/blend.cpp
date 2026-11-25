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

// Straight-alpha Normal over. `sa` is already scaled by layer opacity.
// Identical to the historical blend_pixel Normal tail, including the
// rounding, so a tight span loop can call it without drifting.
inline void over_normal(std::uint8_t* dest, int sr, int sg, int sb, int sa) {
  if (sa <= 0) {
    return;
  }
  if (sa >= 255) {
    dest[0] = static_cast<std::uint8_t>(sr);
    dest[1] = static_cast<std::uint8_t>(sg);
    dest[2] = static_cast<std::uint8_t>(sb);
    dest[3] = 255;
    return;
  }
  const int da = dest[3];
  if (da <= 0) {
    dest[0] = static_cast<std::uint8_t>(sr);
    dest[1] = static_cast<std::uint8_t>(sg);
    dest[2] = static_cast<std::uint8_t>(sb);
    dest[3] = static_cast<std::uint8_t>(sa);
    return;
  }
  const int inv = 255 - sa;
  const int out_a = sa + (da * inv + 127) / 255;
  if (out_a <= 0) {
    dest[0] = dest[1] = dest[2] = dest[3] = 0;
    return;
  }
  dest[0] = clamp_u8((sr * sa + dest[0] * da * inv / 255 + out_a / 2) / out_a);
  dest[1] = clamp_u8((sg * sa + dest[1] * da * inv / 255 + out_a / 2) / out_a);
  dest[2] = clamp_u8((sb * sa + dest[2] * da * inv / 255 + out_a / 2) / out_a);
  dest[3] = clamp_u8(out_a);
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

  if (mode == BlendMode::Normal) {
    over_normal(dest, src[0], src[1], src[2], sa);
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

namespace {

// Normal spans skip the per-pixel bounds checks and the blend-mode switch.
// Opaque runs are copied; clear runs are skipped.
void blend_normal_rect(std::uint8_t* dest, int dest_w, int dest_h, int dest_stride,
                       const std::uint8_t* src, int src_w, int src_h, int src_stride, int offset_x,
                       int offset_y, Rect view, float opacity) {
  if (opacity <= 0.0f) {
    return;
  }
  if (opacity > 1.0f) {
    opacity = 1.0f;
  }
  const bool unit = opacity >= 0.999f;
  int coverage[256];
  if (!unit) {
    for (int a = 0; a < 256; ++a) {
      coverage[a] = static_cast<int>(static_cast<float>(a) * opacity + 0.5f);
    }
  }
  const int vw = std::min(dest_w, view.w);
  const int vh = std::min(dest_h, view.h);
  for (int y = 0; y < vh; ++y) {
    const int src_y = (view.y + y) - offset_y;
    if (src_y < 0 || src_y >= src_h) {
      continue;
    }
    const int x_begin = std::max(0, offset_x - view.x);
    const int x_end = std::min(vw, offset_x + src_w - view.x);
    if (x_begin >= x_end) {
      continue;
    }
    const int src_x0 = (view.x + x_begin) - offset_x;
    std::uint8_t* d = dest + static_cast<std::size_t>(y) * dest_stride +
                      static_cast<std::size_t>(x_begin) * 4;
    const std::uint8_t* s = src + static_cast<std::size_t>(src_y) * src_stride +
                            static_cast<std::size_t>(src_x0) * 4;
    const int n = x_end - x_begin;
    if (unit) {
      int i = 0;
      while (i < n) {
        const int sa = s[3];
        if (sa == 255) {
          int run = 1;
          while (i + run < n && s[static_cast<std::size_t>(run) * 4 + 3] == 255) {
            ++run;
          }
          std::memcpy(d, s, static_cast<std::size_t>(run) * 4);
          i += run;
          s += static_cast<std::size_t>(run) * 4;
          d += static_cast<std::size_t>(run) * 4;
          continue;
        }
        if (sa == 0) {
          int run = 1;
          while (i + run < n && s[static_cast<std::size_t>(run) * 4 + 3] == 0) {
            ++run;
          }
          i += run;
          s += static_cast<std::size_t>(run) * 4;
          d += static_cast<std::size_t>(run) * 4;
          continue;
        }
        over_normal(d, s[0], s[1], s[2], sa);
        ++i;
        s += 4;
        d += 4;
      }
      continue;
    }
    for (int i = 0; i < n; ++i) {
      const int sa = coverage[s[3]];
      if (sa >= 255) {
        d[0] = s[0];
        d[1] = s[1];
        d[2] = s[2];
        d[3] = 255;
      } else if (sa > 0) {
        over_normal(d, s[0], s[1], s[2], sa);
      }
      s += 4;
      d += 4;
    }
  }
}

}  // namespace

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
  if (mode == BlendMode::Normal) {
    blend_normal_rect(dest, dest_w, dest_h, dest_stride, src, src_w, src_h, src_stride, offset_x,
                      offset_y, view, opacity);
    return;
  }
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
      blend_pixel(drow + static_cast<std::size_t>(x) * 4, s, mode, opacity);
      ++x;
    }
  }
}

}  // namespace lundukepaint
