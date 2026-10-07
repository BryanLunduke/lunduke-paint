// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/shapes.hpp"
#include "raster/stroke.hpp"
#include "raster/text.hpp"

#include <cstdio>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Rect;
using lundukepaint::ShapeFillMode;

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_stroke: %s\n", msg);
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
  const int w = 16;
  const int h = 16;
  const Color red{255, 0, 0, 255};
  const Color blue{0, 0, 255, 255};

  // Color eraser replaces FG-similar pixels and leaves others.
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 0);
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        set(buf, w, x, y, (x < 8) ? red : blue);
      }
    }
    Rect dirty{};
    lundukepaint::color_erase_stamp(buf.data(), w, h, w * 4, 4.0, 4.0, 6, red, Color::transparent(), 0,
                                &dirty);
    errors += expect(get(buf, w, 4, 4).fully_transparent(), "erased red punch");
    errors += expect(get(buf, w, 12, 4) == blue, "blue far from stamp stays");
    errors += expect(!dirty.empty(), "color eraser dirty");
  }

  // Spray with a fixed seed writes at least one pixel inside the radius.
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 0);
    std::uint32_t rng = 1;
    Rect dirty{};
    lundukepaint::spray_dots(buf.data(), w, h, w * 4, 8.0, 8.0, 4, 80, red, &rng, &dirty);
    int hits = 0;
    for (int y = 0; y < h; ++y) {
      for (int x = 0; x < w; ++x) {
        if (get(buf, w, x, y) == red) {
          ++hits;
          const int dx = x - 8;
          const int dy = y - 8;
          errors += expect(dx * dx + dy * dy <= 4 * 4 + 2, "spray stays near radius");
        }
      }
    }
    errors += expect(hits > 0, "spray wrote pixels");
    errors += expect(!dirty.empty(), "spray dirty");
  }

  // Filled rounded rect: center is inside, sharp corner of the bbox is outside.
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 0);
    Rect dirty{};
    lundukepaint::draw_rounded_rect(buf.data(), w, h, w * 4, 1, 1, 14, 14, 1, 6, red,
                                ShapeFillMode::Fill, false, &dirty);
    errors += expect(get(buf, w, 8, 8) == red, "rounded center filled");
    errors += expect(get(buf, w, 1, 1) != red, "sharp corner outside radius");
    errors += expect(!dirty.empty(), "rounded dirty");
  }

  // Closed triangle fill contains the centroid; polyline does not fill.
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 0);
    const int xs[3] = {2, 13, 2};
    const int ys[3] = {2, 8, 14};
    Rect dirty{};
    lundukepaint::draw_polygon(buf.data(), w, h, w * 4, xs, ys, 3, 1, red, ShapeFillMode::Fill, false,
                           &dirty);
    errors += expect(get(buf, w, 5, 8) == red, "polygon centroid filled");
    errors += expect(get(buf, w, 14, 1) != red, "outside triangle empty");
  }
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 0);
    Rect dirty{};
    lundukepaint::draw_cubic_bezier(buf.data(), w, h, w * 4, 1, 8, 4, 1, 11, 1, 14, 8, 1, red, false,
                                &dirty);
    errors += expect(get(buf, w, 1, 8) == red, "bezier start");
    errors += expect(get(buf, w, 14, 8) == red, "bezier end");
    errors += expect(!dirty.empty(), "bezier dirty");
  }

  // Text blit path: synthetic 2x2 bitmap stamped onto a buffer.
  {
    std::vector<std::uint8_t> dest(static_cast<std::size_t>(w * h * 4), 0);
    std::vector<std::uint8_t> glyph(2 * 2 * 4, 0);
    glyph[0] = 255;
    glyph[1] = 0;
    glyph[2] = 0;
    glyph[3] = 255;
    glyph[4] = 0;
    glyph[5] = 0;
    glyph[6] = 0;
    glyph[7] = 0;
    Rect dirty{};
    lundukepaint::blit_rgba_buffer(dest.data(), w, h, w * 4, 3, 3, glyph.data(), 2, 2, 8, true, &dirty);
    errors += expect(get(dest, w, 3, 3) == red, "text blit wrote glyph pixel");
    errors += expect(get(dest, w, 4, 3) == Color{0, 0, 0, 0}, "text blit skipped transparent");
    errors += expect(dirty.x == 3 && dirty.y == 3, "text blit dirty origin");
  }
  {
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(32 * 8 * 4), 0);
    Rect dirty{};
    std::uint32_t rng = 1;
    lundukepaint::stroke_spray(buf.data(), 32, 8, 32 * 4, 2, 4, 28, 4, 3, 80, red, &rng, &dirty);
    errors += expect(dirty.w > 20, "spray stroke covers the segment");
    int dots = 0;
    for (int x = 0; x < 32; ++x) {
      for (int y = 0; y < 8; ++y) {
        if (get(buf, 32, x, y).a != 0) {
          ++dots;
        }
      }
    }
    errors += expect(dots > 4, "spray steps place more than one dab");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_stroke: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_stroke: ok\n");
  return 0;
}
