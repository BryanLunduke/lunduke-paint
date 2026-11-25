// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/palette.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace lundukepaint {
namespace {

constexpr Color kMac[] = {{0, 0, 0, 255}, {255, 255, 255, 255}};
constexpr Color kCga[] = {{0, 0, 0, 255}, {85, 255, 255, 255}, {255, 85, 255, 255}, {255, 255, 255, 255}};
constexpr Color kEga[] = {
    {0, 0, 0, 255},     {0, 0, 170, 255},    {0, 170, 0, 255},    {0, 170, 170, 255},
    {170, 0, 0, 255},   {170, 0, 170, 255},  {170, 85, 0, 255},   {170, 170, 170, 255},
    {85, 85, 85, 255},  {85, 85, 255, 255},  {85, 255, 85, 255},  {85, 255, 255, 255},
    {255, 85, 85, 255}, {255, 85, 255, 255}, {255, 255, 85, 255}, {255, 255, 255, 255},
};
constexpr Color kC64[] = {
    {0, 0, 0, 255},       {255, 255, 255, 255}, {136, 0, 0, 255},     {170, 255, 238, 255},
    {204, 68, 204, 255},  {0, 204, 85, 255},    {0, 0, 170, 255},     {238, 238, 119, 255},
    {221, 136, 85, 255},  {102, 68, 0, 255},    {255, 119, 119, 255}, {51, 51, 51, 255},
    {119, 119, 119, 255}, {170, 255, 102, 255}, {0, 136, 255, 255},   {187, 187, 187, 255},
};
constexpr Color kGameBoy[] = {
    {15, 56, 15, 255}, {48, 98, 48, 255}, {139, 172, 15, 255}, {155, 188, 15, 255},
};

constexpr int kBayer[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42}, {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

const RetroPalette kPalettes[] = {
    {"Mac 1-bit 512×342", kMac, 2, 512, 342},
    {"CGA 320×200", kCga, 4, 320, 200},
    {"EGA 640×350", kEga, 16, 640, 350},
    {"VGA 320×200", kEga, 16, 320, 200},
    {"C64 320×200", kC64, 16, 320, 200},
    {"Game Boy 160×144", kGameBoy, 4, 160, 144},
};

int clamp_channel(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 255) {
    return 255;
  }
  return v;
}

int nearest(int r, int g, int b, const Color* colors, int count) {
  int best = 0;
  int best_d = 1 << 30;
  for (int i = 0; i < count; ++i) {
    const int dr = r - colors[i].r;
    const int dg = g - colors[i].g;
    const int db = b - colors[i].b;
    const int d = dr * dr + dg * dg + db * db;
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

void write_rgb(std::uint8_t* p, Color c) {
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
}

}  // namespace

int retro_palette_count() {
  return static_cast<int>(sizeof(kPalettes) / sizeof(kPalettes[0]));
}

const RetroPalette& retro_palette_at(int index) {
  if (index < 0) {
    index = 0;
  }
  if (index >= retro_palette_count()) {
    index = retro_palette_count() - 1;
  }
  return kPalettes[index];
}

void quantize_palette(std::uint8_t* rgba, int width, int height, int stride, const Color* colors,
                      int count, DitherKind kind) {
  if (rgba == nullptr || colors == nullptr || count < 1 || width < 1 || height < 1) {
    return;
  }
  if (kind == DitherKind::FloydSteinberg) {
    std::vector<int> err_r(static_cast<std::size_t>(width) * height, 0);
    std::vector<int> err_g(err_r.size(), 0);
    std::vector<int> err_b(err_r.size(), 0);
    auto add = [&](int x, int y, int er, int eg, int eb, int num) {
      if (x < 0 || y < 0 || x >= width || y >= height) {
        return;
      }
      const std::size_t i = static_cast<std::size_t>(y) * width + x;
      err_r[i] += er * num;
      err_g[i] += eg * num;
      err_b[i] += eb * num;
    };
    for (int y = 0; y < height; ++y) {
      std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
      for (int x = 0; x < width; ++x) {
        std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
        const std::size_t i = static_cast<std::size_t>(y) * width + x;
        const int r = clamp_channel(p[0] + err_r[i] / 16);
        const int g = clamp_channel(p[1] + err_g[i] / 16);
        const int b = clamp_channel(p[2] + err_b[i] / 16);
        const Color chosen = colors[nearest(r, g, b, colors, count)];
        const int er = r - chosen.r;
        const int eg = g - chosen.g;
        const int eb = b - chosen.b;
        write_rgb(p, chosen);
        add(x + 1, y, er, eg, eb, 7);
        add(x - 1, y + 1, er, eg, eb, 3);
        add(x, y + 1, er, eg, eb, 5);
        add(x + 1, y + 1, er, eg, eb, 1);
      }
    }
    return;
  }
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      int r = p[0];
      int g = p[1];
      int b = p[2];
      if (kind == DitherKind::Ordered) {
        const int bayer = kBayer[y & 7][x & 7];
        if (count == 2) {
          const int luma = (r * 30 + g * 59 + b * 11) / 100;
          const int threshold = (bayer * 256) / 64;
          const int luma0 = (colors[0].r * 30 + colors[0].g * 59 + colors[0].b * 11) / 100;
          const int luma1 = (colors[1].r * 30 + colors[1].g * 59 + colors[1].b * 11) / 100;
          const int lighter = luma1 >= luma0 ? 1 : 0;
          write_rgb(p, colors[luma > threshold ? lighter : 1 - lighter]);
          continue;
        }
        const int bias = bayer - 32;
        r = clamp_channel(r + bias);
        g = clamp_channel(g + bias);
        b = clamp_channel(b + bias);
      }
      write_rgb(p, colors[nearest(r, g, b, colors, count)]);
    }
  }
}

}  // namespace lundukepaint
