// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/effects.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace lundukepaint {
namespace {

int clamp_byte(int v) {
  if (v < 0) {
    return 0;
  }
  if (v > 255) {
    return 255;
  }
  return v;
}

void rgb_to_hsv(std::uint8_t r, std::uint8_t g, std::uint8_t b, float& h, float& s, float& v) {
  const float rf = static_cast<float>(r) / 255.0f;
  const float gf = static_cast<float>(g) / 255.0f;
  const float bf = static_cast<float>(b) / 255.0f;
  const float maxc = std::max(rf, std::max(gf, bf));
  const float minc = std::min(rf, std::min(gf, bf));
  const float d = maxc - minc;
  v = maxc;
  s = (maxc <= 0.0f) ? 0.0f : d / maxc;
  if (d <= 1e-6f) {
    h = 0.0f;
    return;
  }
  if (maxc == rf) {
    h = 60.0f * std::fmod((gf - bf) / d, 6.0f);
  } else if (maxc == gf) {
    h = 60.0f * ((bf - rf) / d + 2.0f);
  } else {
    h = 60.0f * ((rf - gf) / d + 4.0f);
  }
  if (h < 0.0f) {
    h += 360.0f;
  }
}

void hsv_to_rgb(float h, float s, float v, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) {
  if (s <= 0.0f) {
    const int gray = clamp_byte(static_cast<int>(v * 255.0f + 0.5f));
    r = g = b = static_cast<std::uint8_t>(gray);
    return;
  }
  h = std::fmod(h, 360.0f);
  if (h < 0.0f) {
    h += 360.0f;
  }
  const float c = v * s;
  const float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
  const float m = v - c;
  float rf = 0;
  float gf = 0;
  float bf = 0;
  if (h < 60.0f) {
    rf = c;
    gf = x;
  } else if (h < 120.0f) {
    rf = x;
    gf = c;
  } else if (h < 180.0f) {
    gf = c;
    bf = x;
  } else if (h < 240.0f) {
    gf = x;
    bf = c;
  } else if (h < 300.0f) {
    rf = x;
    bf = c;
  } else {
    rf = c;
    bf = x;
  }
  r = static_cast<std::uint8_t>(clamp_byte(static_cast<int>((rf + m) * 255.0f + 0.5f)));
  g = static_cast<std::uint8_t>(clamp_byte(static_cast<int>((gf + m) * 255.0f + 0.5f)));
  b = static_cast<std::uint8_t>(clamp_byte(static_cast<int>((bf + m) * 255.0f + 0.5f)));
}

}  // namespace

void invert_rgba(std::uint8_t* rgba, int width, int height, int stride) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return;
  }
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      p[0] = static_cast<std::uint8_t>(255 - p[0]);
      p[1] = static_cast<std::uint8_t>(255 - p[1]);
      p[2] = static_cast<std::uint8_t>(255 - p[2]);
    }
  }
}

void grayscale_rgba(std::uint8_t* rgba, int width, int height, int stride) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return;
  }
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      const int gray = (static_cast<int>(p[0]) * 77 + static_cast<int>(p[1]) * 150 +
                        static_cast<int>(p[2]) * 29) >>
                       8;
      p[0] = p[1] = p[2] = static_cast<std::uint8_t>(gray);
    }
  }
}

void brightness_contrast_rgba(std::uint8_t* rgba, int width, int height, int stride,
                              int brightness, int contrast) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return;
  }
  brightness = std::clamp(brightness, -100, 100);
  contrast = std::clamp(contrast, -100, 100);
  const int add = brightness * 255 / 100;
  const float factor = (100.0f + static_cast<float>(contrast)) / 100.0f;
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      for (int c = 0; c < 3; ++c) {
        const float v = (static_cast<float>(p[c]) - 128.0f) * factor + 128.0f + static_cast<float>(add);
        p[c] = static_cast<std::uint8_t>(clamp_byte(static_cast<int>(v + (v >= 0.0f ? 0.5f : -0.5f))));
      }
    }
  }
}

void hue_saturation_rgba(std::uint8_t* rgba, int width, int height, int stride, int hue,
                         int saturation) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return;
  }
  hue = std::clamp(hue, -180, 180);
  saturation = std::clamp(saturation, -100, 100);
  const float dh = static_cast<float>(hue);
  const float ds = static_cast<float>(saturation) / 100.0f;
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      float h = 0;
      float s = 0;
      float v = 0;
      rgb_to_hsv(p[0], p[1], p[2], h, s, v);
      h += dh;
      s = std::clamp(s + ds, 0.0f, 1.0f);
      hsv_to_rgb(h, s, v, p[0], p[1], p[2]);
    }
  }
}

void posterize_rgba(std::uint8_t* rgba, int width, int height, int stride, int levels) {
  if (rgba == nullptr || width < 1 || height < 1) {
    return;
  }
  levels = std::clamp(levels, 2, 16);
  const int denom = levels - 1;
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      for (int c = 0; c < 3; ++c) {
        const int q = (static_cast<int>(p[c]) * denom + 127) / 255;
        p[c] = static_cast<std::uint8_t>((q * 255) / denom);
      }
    }
  }
}

void box_blur_rgba(const std::uint8_t* src, int width, int height, int src_stride,
                   std::uint8_t* dest, int dest_stride, int radius) {
  box_blur_rect(src, width, height, src_stride, dest, dest_stride, radius, Rect{0, 0, width, height},
                nullptr);
}

void box_blur_rect(const std::uint8_t* src, int width, int height, int src_stride, std::uint8_t* dest,
                   int dest_stride, int radius, Rect region, BlurWork* work) {
  if (work != nullptr) {
    work->scratch_bytes = 0;
    work->pixels_written = 0;
  }
  if (src == nullptr || dest == nullptr || width < 1 || height < 1) {
    return;
  }
  if (region.w <= 0 || region.h <= 0) {
    region = Rect{0, 0, width, height};
  }
  region = rect_intersect(region, Rect{0, 0, width, height});
  if (region.empty()) {
    return;
  }
  if (radius < 1) {
    radius = 1;
  }
  if (radius > 16) {
    radius = 16;
  }
  const int win = radius * 2 + 1;
  const int x0 = region.x;
  const int x1 = region.x2();
  const int y0 = region.y;
  const int y1 = region.y2();
  const int span = x1 - x0;
  const std::size_t scratch = box_blur_scratch_bytes(span, radius);
  std::vector<std::uint32_t> ring(scratch / sizeof(std::uint32_t), 0);
  if (work != nullptr) {
    work->scratch_bytes = scratch;
    work->pixels_written = span * (y1 - y0);
  }

  auto sample = [&](int x, int y, std::uint32_t& r, std::uint32_t& g, std::uint32_t& b,
                    std::uint32_t& a) {
    if (y < 0) {
      y = 0;
    } else if (y >= height) {
      y = height - 1;
    }
    if (x < 0) {
      x = 0;
    } else if (x >= width) {
      x = width - 1;
    }
    const std::uint8_t* p = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(src_stride) +
                            static_cast<std::size_t>(x) * 4;
    a = p[3];
    r = static_cast<std::uint32_t>(p[0]) * a;
    g = static_cast<std::uint32_t>(p[1]) * a;
    b = static_cast<std::uint32_t>(p[2]) * a;
  };

  auto horiz_row = [&](int y, std::uint32_t* out) {
    std::uint32_t sr = 0;
    std::uint32_t sg = 0;
    std::uint32_t sb = 0;
    std::uint32_t sa = 0;
    for (int dx = -radius; dx <= radius; ++dx) {
      std::uint32_t r = 0;
      std::uint32_t g = 0;
      std::uint32_t b = 0;
      std::uint32_t a = 0;
      sample(x0 + dx, y, r, g, b, a);
      sr += r;
      sg += g;
      sb += b;
      sa += a;
    }
    for (int x = x0; x < x1; ++x) {
      const std::size_t i = static_cast<std::size_t>(x - x0) * 4;
      out[i] = sr;
      out[i + 1] = sg;
      out[i + 2] = sb;
      out[i + 3] = sa;
      if (x + 1 >= x1) {
        break;
      }
      std::uint32_t or_ = 0;
      std::uint32_t og = 0;
      std::uint32_t ob = 0;
      std::uint32_t oa = 0;
      std::uint32_t nr = 0;
      std::uint32_t ng = 0;
      std::uint32_t nb = 0;
      std::uint32_t na = 0;
      sample(x - radius, y, or_, og, ob, oa);
      sample(x + 1 + radius, y, nr, ng, nb, na);
      sr = sr + nr - or_;
      sg = sg + ng - og;
      sb = sb + nb - ob;
      sa = sa + na - oa;
    }
  };

  const std::size_t row_floats = static_cast<std::size_t>(span) * 4;
  for (int k = 0; k < win; ++k) {
    int sy = y0 - radius + k;
    if (sy < 0) {
      sy = 0;
    } else if (sy >= height) {
      sy = height - 1;
    }
    horiz_row(sy, ring.data() + static_cast<std::size_t>(k) * row_floats);
  }

  const std::uint32_t area = static_cast<std::uint32_t>(win) * static_cast<std::uint32_t>(win);
  int oldest = 0;
  for (int y = y0; y < y1; ++y) {
    std::uint8_t* drow =
        dest + static_cast<std::size_t>(y) * static_cast<std::size_t>(dest_stride);
    for (int x = 0; x < span; ++x) {
      std::uint32_t sr = 0;
      std::uint32_t sg = 0;
      std::uint32_t sb = 0;
      std::uint32_t sa = 0;
      for (int k = 0; k < win; ++k) {
        const std::uint32_t* cell =
            ring.data() + static_cast<std::size_t>(k) * row_floats + static_cast<std::size_t>(x) * 4;
        sr += cell[0];
        sg += cell[1];
        sb += cell[2];
        sa += cell[3];
      }
      std::uint8_t* o = drow + static_cast<std::size_t>(x0 + x) * 4;
      if (sa == 0) {
        o[0] = o[1] = o[2] = o[3] = 0;
      } else {
        o[0] = static_cast<std::uint8_t>(std::min(sr / sa, 255u));
        o[1] = static_cast<std::uint8_t>(std::min(sg / sa, 255u));
        o[2] = static_cast<std::uint8_t>(std::min(sb / sa, 255u));
        o[3] = static_cast<std::uint8_t>(std::min(sa / area, 255u));
      }
    }
    if (y + 1 >= y1) {
      break;
    }
    int ny = y + 1 + radius;
    if (ny < 0) {
      ny = 0;
    } else if (ny >= height) {
      ny = height - 1;
    }
    horiz_row(ny, ring.data() + static_cast<std::size_t>(oldest) * row_floats);
    oldest = (oldest + 1) % win;
  }
}

namespace {

void conv3(const std::uint8_t* src, int width, int height, int src_stride, std::uint8_t* dest,
           int dest_stride, const int k[9], int bias, bool copy_alpha) {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      int sum[3] = {bias, bias, bias};
      int ki = 0;
      for (int dy = -1; dy <= 1; ++dy) {
        int sy = y + dy;
        if (sy < 0) {
          sy = 0;
        } else if (sy >= height) {
          sy = height - 1;
        }
        for (int dx = -1; dx <= 1; ++dx) {
          int sx = x + dx;
          if (sx < 0) {
            sx = 0;
          } else if (sx >= width) {
            sx = width - 1;
          }
          const std::uint8_t* p =
              src + static_cast<std::size_t>(sy) * static_cast<std::size_t>(src_stride) +
              static_cast<std::size_t>(sx) * 4;
          const int wgt = k[ki++];
          sum[0] += static_cast<int>(p[0]) * wgt;
          sum[1] += static_cast<int>(p[1]) * wgt;
          sum[2] += static_cast<int>(p[2]) * wgt;
        }
      }
      std::uint8_t* o = dest + static_cast<std::size_t>(y) * static_cast<std::size_t>(dest_stride) +
                        static_cast<std::size_t>(x) * 4;
      o[0] = static_cast<std::uint8_t>(clamp_byte(sum[0]));
      o[1] = static_cast<std::uint8_t>(clamp_byte(sum[1]));
      o[2] = static_cast<std::uint8_t>(clamp_byte(sum[2]));
      const std::uint8_t* s =
          src + static_cast<std::size_t>(y) * static_cast<std::size_t>(src_stride) +
          static_cast<std::size_t>(x) * 4;
      o[3] = copy_alpha ? s[3] : static_cast<std::uint8_t>(clamp_byte(s[3]));
    }
  }
}

}  // namespace

void sharpen_rgba(const std::uint8_t* src, int width, int height, int src_stride,
                  std::uint8_t* dest, int dest_stride) {
  if (src == nullptr || dest == nullptr || width < 1 || height < 1) {
    return;
  }
  const int k[9] = {0, -1, 0, -1, 5, -1, 0, -1, 0};
  conv3(src, width, height, src_stride, dest, dest_stride, k, 0, true);
}

void emboss_rgba(const std::uint8_t* src, int width, int height, int src_stride, std::uint8_t* dest,
                 int dest_stride) {
  if (src == nullptr || dest == nullptr || width < 1 || height < 1) {
    return;
  }
  const int k[9] = {-2, -1, 0, -1, 1, 1, 0, 1, 2};
  conv3(src, width, height, src_stride, dest, dest_stride, k, 128, true);
}

}  // namespace lundukepaint
