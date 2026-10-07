// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/document.hpp"
#include "doc/selection.hpp"

#include <cstdio>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Rect;

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_copy_merged: %s\n", msg);
    return 1;
  }
  return 0;
}

Color get(const std::vector<std::uint8_t>& buf, int w, int x, int y) {
  const std::uint8_t* p = buf.data() + static_cast<std::size_t>((y * w + x) * 4);
  return {p[0], p[1], p[2], p[3]};
}

}  // namespace

int main() {
  int errors = 0;
  auto doc = Document::create(4, 2, Color::white(), "Background");
  doc->layers().active_layer().fill(Color{255, 0, 0, 255});
  doc->add_layer();
  doc->layers().active_layer().set_pixel(0, 0, Color{0, 0, 255, 255});

  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgba;
  lundukepaint::copy_merged_rgba(doc->layers(), doc->selection(), doc->width(), doc->height(), w, h,
                             rgba);
  errors += expect(w == 4 && h == 2, "full size");
  errors += expect(get(rgba, w, 0, 0) == Color{0, 0, 255, 255}, "top pixel visible");
  errors += expect(get(rgba, w, 1, 0) == Color{255, 0, 0, 255}, "bottom layer shows");

  doc->selection().set_rect(Rect{0, 0, 1, 1});
  lundukepaint::copy_merged_rgba(doc->layers(), doc->selection(), doc->width(), doc->height(), w, h,
                             rgba);
  errors += expect(w == 1 && h == 1, "clipped size");
  errors += expect(get(rgba, w, 0, 0) == Color{0, 0, 255, 255}, "clipped pixel");

  doc->selection().select_all(doc->width(), doc->height());
  errors += expect(doc->selection().lift(doc->layers().active_layer(), doc->layers().active_index()),
                   "lift the top layer");
  lundukepaint::copy_merged_rgba(doc->layers(), doc->selection(), doc->width(), doc->height(), w, h,
                             rgba);
  errors += expect(w == 4 && h == 2, "floating copy is the selection");
  if (w == 4 && h == 2) {
    errors += expect(get(rgba, w, 0, 0) == Color{0, 0, 255, 255}, "float covers its pixel");
    errors += expect(get(rgba, w, 1, 0) == Color{255, 0, 0, 255}, "hole keeps the layer below");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_copy_merged: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_copy_merged: ok\n");
  return 0;
}
