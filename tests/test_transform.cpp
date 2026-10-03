// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_image.hpp"
#include "doc/document.hpp"
#include "raster/transform.hpp"

#include <cstdio>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Layer;
using lundukepaint::LayerBufferCommand;

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_transform: %s\n", msg);
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int errors = 0;

  // 2×2 rotate 90 CW: [A B]    [C A]
  //                   [C D] -> [D B]
  {
    const int w = 2;
    const int h = 2;
    std::vector<std::uint8_t> src(16, 0);
    auto set = [&](int x, int y, Color c) {
      std::uint8_t* p = src.data() + static_cast<std::size_t>((y * w + x) * 4);
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
      p[3] = c.a;
    };
    auto get = [&](const std::vector<std::uint8_t>& buf, int x, int y, int bw) {
      const std::uint8_t* p = buf.data() + static_cast<std::size_t>((y * bw + x) * 4);
      return Color{p[0], p[1], p[2], p[3]};
    };
    const Color A{1, 0, 0, 255};
    const Color B{2, 0, 0, 255};
    const Color C{3, 0, 0, 255};
    const Color D{4, 0, 0, 255};
    set(0, 0, A);
    set(1, 0, B);
    set(0, 1, C);
    set(1, 1, D);
    std::vector<std::uint8_t> dest(16, 0);
    lundukepaint::rotate_90_cw(src.data(), w, h, w * 4, dest.data(), h * 4);
    errors += expect(get(dest, 0, 0, 2) == C, "CW top-left is C");
    errors += expect(get(dest, 1, 0, 2) == A, "CW top-right is A");
    errors += expect(get(dest, 0, 1, 2) == D, "CW bottom-left is D");
    errors += expect(get(dest, 1, 1, 2) == B, "CW bottom-right is B");

    std::vector<std::uint8_t> ccw(16, 0);
    lundukepaint::rotate_90_ccw(src.data(), w, h, w * 4, ccw.data(), h * 4);
    errors += expect(get(ccw, 0, 0, 2) == B, "CCW top-left is B");
    errors += expect(get(ccw, 1, 0, 2) == D, "CCW top-right is D");
    errors += expect(get(ccw, 0, 1, 2) == A, "CCW bottom-left is A");
    errors += expect(get(ccw, 1, 1, 2) == C, "CCW bottom-right is C");

    std::vector<std::uint8_t> flipped = src;
    lundukepaint::flip_h(flipped.data(), w, h, w * 4);
    errors += expect(get(flipped, 0, 0, 2) == B, "flip H top-left is B");
    errors += expect(get(flipped, 1, 0, 2) == A, "flip H top-right is A");

    auto doc = Document::create(2, 2, Color::transparent(), "Background");
    doc->layers().active_layer().set_pixels(2, 2, src.data(), 8);
    auto cmd = LayerBufferCommand::from_buffers("Rotate 90", 2, 2, src.data(), 8, 2, 2, dest.data(),
                                                8, 0);
    doc->commit(std::move(cmd));
    errors += expect(doc->layers().active_layer().pixel(0, 0) == C, "apply rotate");
    doc->undo();
    errors += expect(doc->layers().active_layer().pixel(0, 0) == A, "undo rotate restores");
    doc->redo();
    errors += expect(doc->layers().active_layer().pixel(1, 1) == B, "redo rotate");

    std::vector<std::uint8_t> after_flip = src;
    lundukepaint::flip_v(after_flip.data(), w, h, w * 4);
    auto cmd2 = LayerBufferCommand::from_buffers("Flip vertical", 2, 2, dest.data(), 8, 2, 2,
                                                 after_flip.data(), 8, 0);
    doc->commit(std::move(cmd2));
    doc->undo();
    errors += expect(doc->layers().active_layer().pixel(0, 0) == C, "undo flip restores rotate");
  }

  {
    const int width = 5;
    const int height = 4;
    std::vector<std::uint8_t> img(static_cast<std::size_t>(width * height * 4), 0);
    auto put = [&](int x, int y, Color c) {
      std::uint8_t* p = img.data() + static_cast<std::size_t>((y * width + x) * 4);
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
      p[3] = c.a;
    };
    const Color red{200, 0, 0, 255};
    const Color green{0, 180, 0, 255};
    const Color blue{0, 0, 220, 255};
    for (int x = 0; x < width; ++x) {
      put(x, 0, red);
    }
    for (int y = 1; y < height; ++y) {
      put(0, y, green);
      for (int x = 1; x < width; ++x) {
        put(x, y, blue);
      }
    }
    const lundukepaint::Rect crop = lundukepaint::autocrop_bounds(img.data(), width, height, width * 4);
    errors += expect(crop.x == 1 && crop.y == 1 && crop.w == 4 && crop.h == 3,
                     "autocrop drops top border and side margin");
  }

  {
    const Color red{9, 0, 0, 255};
    const Color blue{0, 8, 0, 255};
    std::vector<std::uint8_t> small(2 * 2 * 4, 0);
    auto put = [&](int x, int y, Color c) {
      std::uint8_t* p = small.data() + static_cast<std::size_t>((y * 2 + x) * 4);
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
      p[3] = c.a;
    };
    put(0, 0, red);
    put(1, 0, blue);
    put(0, 1, blue);
    put(1, 1, red);
    const lundukepaint::PlacedPixels flipped =
        lundukepaint::place_flip_h(small.data(), 2, 2, 8, 0, 0, 4, 2);
    errors += expect(flipped.width == 2 && flipped.height == 2, "flip keeps the small buffer");
    errors += expect(flipped.offset_x == 2 && flipped.offset_y == 0, "flip mirrors the offset");
    errors += expect(static_cast<int>(flipped.pixels.size()) == 2 * 2 * 4, "flip buffer is layer-sized");
    const std::uint8_t* p = flipped.pixels.data();
    errors += expect(p[0] == blue.r && p[4] == red.r, "flip swaps the top row");

    const lundukepaint::PlacedPixels turned =
        lundukepaint::place_rotate_180(small.data(), 2, 2, 8, 1, 0, 6, 4);
    errors += expect(turned.width == 2 && turned.height == 2, "rotate 180 keeps the small buffer");
    errors += expect(turned.offset_x == 3 && turned.offset_y == 2, "rotate 180 updates offset");
    errors += expect(static_cast<int>(turned.pixels.size()) == 16, "rotate 180 does not grow to the canvas");

    std::vector<std::uint8_t> full = small;
    lundukepaint::flip_h(full.data(), 2, 2, 8);
    const lundukepaint::PlacedPixels same =
        lundukepaint::place_flip_h(small.data(), 2, 2, 8, 0, 0, 2, 2);
    errors += expect(same.offset_x == 0 && same.offset_y == 0, "canvas-sized flip keeps offset 0");
    errors += expect(same.pixels == full, "canvas-sized flip matches flip_h");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_transform: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_transform: ok\n");
  return 0;
}
