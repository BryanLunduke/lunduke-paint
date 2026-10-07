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
  {
    auto doc = Document::create(4, 4, Color::white(), "Background");
    Layer& layer = doc->layers().active_layer();
    layer.set_offset(1, 1);
    layer.set_pixel(0, 0, Color{0, 255, 0, 255});
    Selection& sel = doc->selection();
    sel.set_rect(Rect{1, 1, 1, 1});
    errors += expect(sel.lift(layer, 0), "lift uses layer space");
    errors += expect(sel.float_pixel(0, 0) == (Color{0, 255, 0, 255}), "lifted the offset pixel");
    errors += expect(doc->commit_floating(), "commit back to the source layer");
    errors += expect(layer.pixel(0, 0) == (Color{0, 255, 0, 255}), "commit writes layer space");
  }

  // Mask covers only (0,0) inside an 8x6 selection. Stamp, delete, and
  // copy-blit must leave (1,0) alone and must not promote the mask to a rect.
  {
    auto doc = Document::create(8, 6, Color::white(), "Background");
    Layer& layer = doc->layers().active_layer();
    layer.fill(Color{200, 0, 0, 255});
    layer.set_pixel(0, 0, Color{0, 180, 0, 255});
    layer.set_pixel(1, 0, Color{0, 0, 180, 255});
    const Color outside = layer.pixel(1, 0);
    Selection& sel = doc->selection();
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(8 * 6), 0);
    mask[0] = 255;
    sel.set_mask({0, 0, 8, 6}, mask);
    errors += expect(sel.lift(layer, 0), "masked lift");
    errors += expect(doc->commit_floating(), "masked commit");
    errors += expect(layer.pixel(1, 0) == outside, "commit leaves (1,0)");
    errors += expect(sel.has_mask(), "commit keeps the mask");
    errors += expect(sel.contains(0, 0) && !sel.contains(1, 0), "mask still covers only (0,0)");
  }
  {
    auto doc = Document::create(8, 6, Color::white(), "Background");
    Layer& layer = doc->layers().active_layer();
    layer.fill(Color{200, 0, 0, 255});
    layer.set_pixel(0, 0, Color{0, 180, 0, 255});
    layer.set_pixel(1, 0, Color{0, 0, 180, 255});
    const Color outside = layer.pixel(1, 0);
    Selection& sel = doc->selection();
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(8 * 6), 0);
    mask[0] = 255;
    sel.set_mask({0, 0, 8, 6}, mask);
    errors += expect(sel.lift(layer, 0), "masked lift for delete");
    doc->delete_selection();
    errors += expect(layer.pixel(1, 0) == outside, "delete leaves (1,0)");
    errors += expect(layer.pixel(0, 0).a == 0, "delete clears the covered pixel");
  }
  {
    auto doc = Document::create(8, 6, Color::white(), "Background");
    Layer& layer = doc->layers().active_layer();
    layer.fill(Color{200, 0, 0, 255});
    layer.set_pixel(0, 0, Color{0, 180, 0, 255});
    layer.set_pixel(1, 0, Color{0, 0, 180, 255});
    const Color kept = layer.pixel(1, 0);
    const Color neighbor = layer.pixel(3, 0);
    Selection& sel = doc->selection();
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(8 * 6), 0);
    mask[0] = 255;
    sel.set_mask({0, 0, 8, 6}, mask);
    errors += expect(sel.lift(layer, 0), "masked lift for copy");
    sel.set_copy_mode(true);
    sel.move_float(2, 0);
    errors += expect(doc->commit_floating(), "copy-mode commit");
    errors += expect(layer.pixel(0, 0) == (Color{0, 180, 0, 255}), "copy leaves the origin");
    errors += expect(layer.pixel(1, 0) == kept, "copy leaves (1,0)");
    errors += expect(layer.pixel(2, 0) == (Color{0, 180, 0, 255}), "copy writes the covered pixel");
    errors += expect(layer.pixel(3, 0) == neighbor, "copy leaves the rest of the bbox");
  }
  {
    auto doc = Document::create(8, 6, Color::white(), "Background");
    Layer& layer = doc->layers().active_layer();
    layer.fill(Color{200, 0, 0, 255});
    layer.set_pixel(0, 0, Color{0, 180, 0, 255});
    Selection& sel = doc->selection();
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(8 * 6), 0);
    mask[0] = 255;
    sel.set_mask({0, 0, 8, 6}, std::move(mask));
    errors += expect(sel.lift(layer, 0), "masked lift for preview");
    const Rect view{0, 0, 8, 6};
    std::vector<std::uint8_t> flat(static_cast<std::size_t>(8 * 6 * 4), 0);
    doc->layers().composite_rect(flat.data(), 8 * 4, view);
    const Color composite_outside{flat[4], flat[5], flat[6], flat[7]};
    lundukepaint::paint_floating_selection(doc->layers(), sel, flat.data(), 8 * 4, view, false,
                                           Color::transparent(), Color::transparent());
    errors += expect((Color{flat[4], flat[5], flat[6], flat[7]}) == composite_outside,
                     "preview leaves outside-mask pixel equal to the composite");
  }
  {
    auto doc = Document::create(8, 6, Color::white(), "Background");
    doc->set_dirty(true);
    Layer& layer = doc->layers().active_layer();
    layer.fill(Color{10, 20, 30, 255});
    const Color outside = layer.pixel(1, 0);
    Selection& sel = doc->selection();
    sel.set_rect({0, 0, 2, 1});
    errors += expect(sel.lift(layer, 0), "lift before lock");
    doc->set_layer_locked(0, true);
    errors += expect(!doc->commit_floating(), "locked layer refuses the stamp");
    errors += expect(sel.floating(), "float stays up");
    errors += expect(layer.pixel(1, 0) == outside, "failed stamp leaves outside pixels");
    if (doc->commit_floating()) {
      doc->mark_clean();
    }
    errors += expect(doc->dirty(), "failed stamp does not clear dirty");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_selection: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_selection: ok\n");
  return 0;
}
