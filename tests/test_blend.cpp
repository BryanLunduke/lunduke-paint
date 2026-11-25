// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/blend.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

using lundukepaint::BlendMode;
using lundukepaint::Rect;
using lundukepaint::blend_layer_rect;
using lundukepaint::blend_pixel;

// The Normal formula from before the span loop, kept here so the fast path
// has to match it byte for byte.
void reference_normal(std::uint8_t* dest, const std::uint8_t* src, float opacity) {
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
  const int inv = 255 - sa;
  const int out_a = sa + (da * inv + 127) / 255;
  if (out_a <= 0) {
    dest[0] = dest[1] = dest[2] = dest[3] = 0;
    return;
  }
  const int out_r = (src[0] * sa + dest[0] * da * inv / 255 + out_a / 2) / out_a;
  const int out_g = (src[1] * sa + dest[1] * da * inv / 255 + out_a / 2) / out_a;
  const int out_b = (src[2] * sa + dest[2] * da * inv / 255 + out_a / 2) / out_a;
  auto clamp = [](int v) {
    if (v < 0) {
      return 0;
    }
    if (v > 255) {
      return 255;
    }
    return v;
  };
  dest[0] = static_cast<std::uint8_t>(clamp(out_r));
  dest[1] = static_cast<std::uint8_t>(clamp(out_g));
  dest[2] = static_cast<std::uint8_t>(clamp(out_b));
  dest[3] = static_cast<std::uint8_t>(clamp(out_a));
}

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_blend: %s\n", msg);
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int errors = 0;

  // Multiply red 255,0,0 opaque over white: result red.
  {
    std::uint8_t dest[4] = {255, 255, 255, 255};
    const std::uint8_t src[4] = {255, 0, 0, 255};
    blend_pixel(dest, src, BlendMode::Multiply, 1.0f);
    errors += expect(dest[0] == 255 && dest[1] == 0 && dest[2] == 0 && dest[3] == 255,
                     "multiply red over white");
  }

  // Multiply 128,128,128 over 128,128,128: ~64,64,64
  {
    std::uint8_t dest[4] = {128, 128, 128, 255};
    const std::uint8_t src[4] = {128, 128, 128, 255};
    blend_pixel(dest, src, BlendMode::Multiply, 1.0f);
    errors += expect(dest[0] >= 63 && dest[0] <= 65, "multiply gray r");
    errors += expect(dest[1] >= 63 && dest[1] <= 65, "multiply gray g");
    errors += expect(dest[2] >= 63 && dest[2] <= 65, "multiply gray b");
    errors += expect(dest[3] == 255, "multiply gray a");
  }

  // Screen black over mid gray stays mid gray; screen white goes white.
  {
    std::uint8_t dest[4] = {128, 128, 128, 255};
    const std::uint8_t src[4] = {255, 255, 255, 255};
    blend_pixel(dest, src, BlendMode::Screen, 1.0f);
    errors += expect(dest[0] == 255 && dest[1] == 255 && dest[2] == 255, "screen white");
  }

  // Hidden-style: zero opacity leaves dest unchanged.
  {
    std::uint8_t dest[4] = {10, 20, 30, 255};
    const std::uint8_t src[4] = {255, 0, 0, 255};
    blend_pixel(dest, src, BlendMode::Normal, 0.0f);
    errors += expect(dest[0] == 10 && dest[1] == 20 && dest[2] == 30 && dest[3] == 255,
                     "zero opacity is a no-op");
  }

  // Transparent dest takes source (even for Multiply).
  {
    std::uint8_t dest[4] = {0, 0, 0, 0};
    const std::uint8_t src[4] = {40, 80, 120, 200};
    blend_pixel(dest, src, BlendMode::Multiply, 1.0f);
    errors += expect(dest[0] == 40 && dest[1] == 80 && dest[2] == 120 && dest[3] == 200,
                     "multiply onto transparent dest");
  }

  // Span loop versus the old per-pixel Normal formula.
  {
    constexpr int kW = 48;
    constexpr int kH = 20;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(kW) * kH * 4);
    std::vector<std::uint8_t> fast(static_cast<std::size_t>(kW) * kH * 4);
    std::vector<std::uint8_t> slow = fast;
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) {
        std::uint8_t* s = src.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
        std::uint8_t* d = fast.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
        s[0] = static_cast<std::uint8_t>(x * 5);
        s[1] = static_cast<std::uint8_t>(y * 11);
        s[2] = static_cast<std::uint8_t>(180 - x);
        s[3] = static_cast<std::uint8_t>((x + y * 3) * 7);
        if ((x + y) % 11 == 0) {
          s[3] = 0;
        }
        if ((x + y) % 9 == 0) {
          s[3] = 255;
        }
        d[0] = static_cast<std::uint8_t>(255 - x * 3);
        d[1] = static_cast<std::uint8_t>(y * 9);
        d[2] = 40;
        d[3] = static_cast<std::uint8_t>(x % 5 == 0 ? 0 : 200);
      }
    }
    slow = fast;
    const Rect view{3, 2, 40, 16};
    blend_layer_rect(fast.data(), kW, kH, kW * 4, src.data(), kW, kH, kW * 4, 2, 1, view,
                     BlendMode::Normal, 0.65f);
    for (int y = 0; y < view.h; ++y) {
      for (int x = 0; x < view.w; ++x) {
        const int sx = view.x + x - 2;
        const int sy = view.y + y - 1;
        if (sx < 0 || sy < 0 || sx >= kW || sy >= kH) {
          continue;
        }
        std::uint8_t* d = slow.data() + (static_cast<std::size_t>(y) * kW + x) * 4;
        const std::uint8_t* s = src.data() + (static_cast<std::size_t>(sy) * kW + sx) * 4;
        reference_normal(d, s, 0.65f);
      }
    }
    // The fast path writes view rows at dest y, not document y. Compare those rows.
    bool match = true;
    for (int y = 0; y < view.h && match; ++y) {
      if (std::memcmp(fast.data() + static_cast<std::size_t>(y) * kW * 4,
                      slow.data() + static_cast<std::size_t>(y) * kW * 4,
                      static_cast<std::size_t>(kW) * 4) != 0) {
        match = false;
      }
    }
    errors += expect(match, "normal span loop matches the old per-pixel formula");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_blend: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_blend: ok\n");
  return 0;
}
