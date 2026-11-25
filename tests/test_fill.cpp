// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/document.hpp"
#include "raster/fill.hpp"
#include "raster/gradient.hpp"

#include <cstdio>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Rect;

void fail(const char* msg) {
  std::fprintf(stderr, "test_fill: %s\n", msg);
  std::fflush(stderr);
}

int expect(bool cond, const char* msg) {
  if (!cond) {
    fail(msg);
    return 1;
  }
  return 0;
}

Color get(const std::vector<std::uint8_t>& buf, int w, int x, int y) {
  const std::uint8_t* p = buf.data() + static_cast<std::size_t>((y * w + x) * 4);
  return {p[0], p[1], p[2], p[3]};
}

void set(std::vector<std::uint8_t>& buf, int w, int x, int y, Color c) {
  std::uint8_t* p = buf.data() + static_cast<std::size_t>((y * w + x) * 4);
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

}  // namespace

int main() {
  int errors = 0;
  const int w = 8;
  const int h = 8;
  const Color white = Color::white();
  const Color black = Color::black();
  const Color red{255, 0, 0, 255};
  const Color gray{10, 10, 10, 255};

  // Tolerance 0: exact match only.
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 255);
    set(buf, w, 3, 3, black);
    set(buf, w, 4, 3, black);
    set(buf, w, 3, 4, gray);
    Rect dirty{};
    lundukepaint::flood_fill(buf.data(), w, h, w * 4, 0, 0, red, 0, &dirty);
    errors += expect(get(buf, w, 0, 0) == red, "fill (0,0) should be red");
    errors += expect(get(buf, w, 7, 7) == red, "connected white should fill");
    errors += expect(get(buf, w, 3, 3) == black, "black island stays black at tol 0");
    errors += expect(get(buf, w, 3, 4) == gray, "near-black stays at tol 0");
    errors += expect(!dirty.empty(), "dirty rect from fill");
  }

  // Tolerance > 0: gray is similar to black.
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 0);
    for (int i = 0; i < w * h; ++i) {
      buf[static_cast<std::size_t>(i) * 4 + 3] = 255;
    }
    // black field with a slightly-off pixel and a distant red wall
    set(buf, w, 2, 2, gray);
    for (int y = 0; y < h; ++y) {
      set(buf, w, 6, y, red);
    }
    Rect dirty{};
    lundukepaint::flood_fill(buf.data(), w, h, w * 4, 0, 0, white, 16, &dirty);
    errors += expect(get(buf, w, 0, 0) == white, "seed filled");
    errors += expect(get(buf, w, 2, 2) == white, "gray within tolerance filled");
    errors += expect(get(buf, w, 6, 0) == red, "red wall not filled");
    errors += expect(get(buf, w, 7, 0) == Color{0, 0, 0, 255},
                     "pixels beyond wall stay seed-unlike / unfilled");
  }

  errors += expect(lundukepaint::mutation_main_loop_pumps() == 0, "flood fill does not pump the main loop");
  {
    auto doc = lundukepaint::Document::create(4, 4, Color::white(), "Lower");
    doc->add_layer();
    errors += expect(doc->merge_down(), "merge down for the pump hook");
    errors += expect(lundukepaint::mutation_main_loop_pumps() == 0,
                     "merge down does not pump the main loop");
  }

  {
    constexpr int kW = 16;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(kW) * 8 * 4, 0);
    lundukepaint::gradient_dither(buf.data(), kW, 8, kW * 4, 0, 0, kW - 1, 0, Color::black(),
                                  Color::white(), lundukepaint::GradientKind::Linear, nullptr, 0, 0, 0,
                                  0, nullptr);
    const Color left = get(buf, kW, 0, 0);
    const Color right = get(buf, kW, kW - 1, 0);
    errors += expect(left == Color::black(), "linear dither starts on the first color");
    errors += expect(right == Color::white(), "linear dither ends on the second color");
    int flips = 0;
    Color prev = left;
    for (int x = 1; x < kW; ++x) {
      const Color c = get(buf, kW, x, 3);
      if (c != prev && (c == Color::black() || c == Color::white())) {
        ++flips;
        prev = c;
      }
    }
    errors += expect(flips >= 2, "linear dither mixes both colors across the span");

    std::vector<std::uint8_t> radial(static_cast<std::size_t>(kW) * kW * 4, 0);
    lundukepaint::gradient_dither(radial.data(), kW, kW, kW * 4, kW / 2, kW / 2, kW - 1, kW / 2,
                                  Color::black(), Color::white(), lundukepaint::GradientKind::Radial,
                                  nullptr, 0, 0, 0, 0, nullptr);
    errors += expect(get(radial, kW, kW / 2, kW / 2) == Color::black(), "radial center is the first color");
    errors += expect(get(radial, kW, kW - 1, kW / 2) == Color::white(), "radial rim is the second color");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_fill: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_fill: ok\n");
  return 0;
}
