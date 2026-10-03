// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/shapes.hpp"

#include <cstdio>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Layer;
using lundukepaint::PixelPatchCommand;
using lundukepaint::Rect;
using lundukepaint::Selection;

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_selection: %s\n", msg);
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  int errors = 0;
  auto doc = Document::create(8, 6, Color::white(), "Background");
  Layer& layer = doc->layers().active_layer();
  layer.fill_rect({1, 1, 3, 3}, Color::black());

  Selection sel;
  errors += expect(sel.empty(), "new selection is empty");
  sel.set_rect({1, 1, 3, 3});
  errors += expect(sel.contains(2, 2), "rect contains interior");
  errors += expect(!sel.contains(0, 0), "rect excludes outside");
  sel.invert(8, 6);
  errors += expect(sel.contains(0, 0), "invert includes outside");
  errors += expect(!sel.contains(2, 2), "invert excludes hole");
  sel.invert(8, 6);
  errors += expect(sel.contains(2, 2), "invert twice restores rect");

  // Delete (fill with transparency) is undoable.
  Layer before(layer.width(), layer.height(), Color::transparent(), "before");
  before.copy_from(layer);
  Rect dirty{};
  lundukepaint::fill_selection(layer, sel, Color::transparent(), &dirty);
  errors += expect(layer.pixel(2, 2).fully_transparent(), "delete punches transparency");
  errors += expect(layer.pixel(0, 0) == Color::white(), "outside selection stays");
  auto cmd = PixelPatchCommand::from_layers(before, layer, dirty, "Delete");
  errors += expect(!cmd->empty(), "delete produced a patch");
  doc->history().commit_applied(std::move(cmd));
  doc->history().undo(*doc);
  errors += expect(layer.pixel(2, 2) == Color::black(), "undo delete restores pixels");
  doc->history().redo(*doc);
  errors += expect(layer.pixel(2, 2).fully_transparent(), "redo delete punches again");

  // Crop helper: copy the selection rect.
  int cw = 0;
  int ch = 0;
  std::vector<std::uint8_t> copied;
  sel.set_rect({1, 1, 3, 3});
  // After redo the hole is transparent; recopy from a filled source.
  layer.fill_rect({1, 1, 3, 3}, Color{10, 20, 30, 255});
  lundukepaint::copy_selection_rgba(layer, sel, 8, 6, cw, ch, copied);
  errors += expect(cw == 3 && ch == 3, "copy size matches rect");
  errors += expect(copied.size() == 3u * 3u * 4u, "copy buffer size");
  errors += expect(copied[0] == 10 && copied[1] == 20 && copied[2] == 30 && copied[3] == 255,
                   "copied top-left pixel");

  // Ellipse / lasso masks contain a known interior pixel.
  {
    Selection ellipse;
    const int ew = 12;
    const int eh = 10;
    std::vector<std::uint8_t> emask(static_cast<std::size_t>(ew * eh), 0);
    lundukepaint::fill_ellipse_mask(emask.data(), ew, eh);
    ellipse.set_mask({1, 1, ew, eh}, std::move(emask));
    errors += expect(ellipse.contains(1 + ew / 2, 1 + eh / 2), "ellipse center selected");
    errors += expect(!ellipse.contains(1, 1), "ellipse sharp corner not selected");
    errors += expect(!ellipse.contains(0, 0), "outside ellipse bbox");
  }
  {
    Selection lasso;
    const int xs[3] = {0, 7, 0};
    const int ys[3] = {0, 4, 7};
    const int w = 8;
    const int h = 8;
    std::vector<std::uint8_t> lmask(static_cast<std::size_t>(w * h), 0);
    lundukepaint::fill_polygon_mask(lmask.data(), w, h, xs, ys, 3);
    lasso.set_mask({2, 2, w, h}, std::move(lmask));
    errors += expect(lasso.contains(2 + 2, 2 + 4), "lasso interior selected");
    errors += expect(!lasso.contains(2 + 7, 2 + 0), "lasso far corner not selected");
  }

  {
    Selection all;
    all.select_all(8, 6);
    all.invert(8, 6);
    errors += expect(all.empty(), "select all inverts to empty");
  }
  {
    Selection masked;
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(8 * 6), 0);
    mask[0] = 255;
    masked.set_mask({0, 0, 8, 6}, std::move(mask));
    errors += expect(masked.contains(0, 0), "wand pixel selected");
    errors += expect(!masked.contains(1, 0), "rest of canvas not selected");
    masked.invert(8, 6);
    errors += expect(!masked.empty(), "full-canvas mask still inverts");
    errors += expect(masked.inverted(), "mask invert sets the flag");
    errors += expect(!masked.contains(0, 0), "inverted mask drops the wand pixel");
    errors += expect(masked.contains(1, 0), "inverted mask keeps the complement");
    masked.invert(8, 6);
    errors += expect(masked.contains(0, 0) && !masked.contains(1, 0), "second invert restores mask");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_selection: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_selection: ok\n");
  return 0;
}
