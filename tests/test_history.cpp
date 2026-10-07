// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_layers.hpp"
#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "raster/blend.hpp"
#include "raster/fill.hpp"
#include "raster/stroke.hpp"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Layer;
using lundukepaint::PixelPatchCommand;
using lundukepaint::Rect;

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_history: %s\n", msg);
    return 1;
  }
  return 0;
}

std::vector<std::uint8_t> snapshot(const Layer& layer) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(layer.stride()) *
                                static_cast<std::size_t>(layer.height()));
  std::copy(layer.pixels(), layer.pixels() + out.size(), out.begin());
  return out;
}

bool same_pixels(const Layer& a, const std::vector<std::uint8_t>& b) {
  const std::size_t n = static_cast<std::size_t>(a.stride()) * static_cast<std::size_t>(a.height());
  return std::equal(a.pixels(), a.pixels() + n, b.begin());
}

}  // namespace

int main() {
  int errors = 0;
  auto doc = Document::create(16, 12, Color::white(), "Background");
  Layer& layer = doc->layers().active_layer();
  const auto original = snapshot(layer);

  // Stroke command: pencil scribble, one history item, undo invertibility.
  {
    Layer before(layer.width(), layer.height(), Color::transparent(), "before");
    before.copy_from(layer);
    Rect dirty{};
    lundukepaint::stroke_pencil(layer.pixels(), layer.width(), layer.height(), layer.stride(), 1, 1, 10,
                            7, 2, Color::black(), &dirty);
    Layer after(layer.width(), layer.height(), Color::transparent(), "after");
    after.copy_from(layer);
    auto cmd = PixelPatchCommand::from_layers(before, after, dirty, "Pencil stroke");
    errors += expect(!cmd->empty(), "stroke produced a dirty patch");
    // Command already applied on the layer; record without re-applying.
    doc->history().commit_applied(std::move(cmd));
    errors += expect(doc->history().can_undo(), "can undo stroke");
    const auto stroked = snapshot(layer);
    errors += expect(!same_pixels(layer, original), "stroke changed pixels");

    doc->history().undo(*doc);
    errors += expect(same_pixels(layer, original), "undo stroke restores pixels");
    errors += expect(doc->history().can_redo(), "can redo stroke");

    doc->history().redo(*doc);
    errors += expect(same_pixels(layer, stroked), "redo stroke matches after");
    doc->history().undo(*doc);
  }

  // Fill command undo invertibility.
  {
    Layer before(layer.width(), layer.height(), Color::transparent(), "before");
    before.copy_from(layer);
    Rect dirty{};
    lundukepaint::flood_fill(layer.pixels(), layer.width(), layer.height(), layer.stride(), 0, 0,
                         Color{0, 128, 255, 255}, 0, &dirty);
    Layer after(layer.width(), layer.height(), Color::transparent(), "after");
    after.copy_from(layer);
    auto cmd = PixelPatchCommand::from_layers(before, after, dirty, "Flood fill");
    errors += expect(!cmd->empty(), "fill produced a dirty patch");
    doc->history().commit_applied(std::move(cmd));
    const auto filled = snapshot(layer);
    errors += expect(layer.pixel(0, 0) == (Color{0, 128, 255, 255}), "fill wrote seed");

    doc->history().undo(*doc);
    errors += expect(same_pixels(layer, original), "undo fill restores original");
    doc->history().redo(*doc);
    errors += expect(same_pixels(layer, filled), "redo fill matches after");
  }

  // Click-to-jump: two stacked commands, then undo/redo by index.
  {
    doc->history().clear();
    layer.fill(Color::white());
    const auto start = snapshot(layer);

    Layer before1(layer.width(), layer.height(), Color::transparent(), "b1");
    before1.copy_from(layer);
    Rect dirty1{};
    lundukepaint::stroke_pencil(layer.pixels(), layer.width(), layer.height(), layer.stride(), 2, 2, 6,
                            4, 1, Color::black(), &dirty1);
    Layer after1(layer.width(), layer.height(), Color::transparent(), "a1");
    after1.copy_from(layer);
    doc->history().commit_applied(
        PixelPatchCommand::from_layers(before1, after1, dirty1, "Pencil stroke"));
    const auto stroked = snapshot(layer);

    Layer before2(layer.width(), layer.height(), Color::transparent(), "b2");
    before2.copy_from(layer);
    Rect dirty2{};
    lundukepaint::flood_fill(layer.pixels(), layer.width(), layer.height(), layer.stride(), 0, 0,
                         Color{0, 128, 255, 255}, 0, &dirty2);
    Layer after2(layer.width(), layer.height(), Color::transparent(), "a2");
    after2.copy_from(layer);
    doc->history().commit_applied(
        PixelPatchCommand::from_layers(before2, after2, dirty2, "Flood fill"));
    const auto filled = snapshot(layer);

    errors += expect(doc->history().count() == 2, "two commands recorded");
    errors += expect(doc->history().index() == 1, "index at latest");
    errors += expect(doc->history().name_at(0) == "Pencil stroke", "first name");
    errors += expect(doc->history().name_at(1) == "Flood fill", "second name");

    doc->jump_history(0);
    errors += expect(doc->history().index() == 0, "jump back to stroke");
    errors += expect(same_pixels(layer, stroked), "jump undid fill");
    doc->jump_history(-1);
    errors += expect(doc->history().index() == -1, "jump to initial");
    errors += expect(same_pixels(layer, start), "jump to initial restores");
    doc->jump_history(1);
    errors += expect(doc->history().index() == 1, "jump forward to fill");
    errors += expect(same_pixels(layer, filled), "jump redo matches fill");
  }

  {
    auto clean = Document::create(4, 4, Color::white(), "Background");
    errors += expect(!clean->dirty(), "new document is clean");
    Layer& paint = clean->layers().active_layer();
    Layer before(paint.width(), paint.height(), Color::transparent(), "before");
    before.copy_from(paint);
    Rect dirty{};
    lundukepaint::stroke_pencil(paint.pixels(), paint.width(), paint.height(), paint.stride(), 0, 0, 2,
                            2, 1, Color::black(), &dirty);
    clean->commit(PixelPatchCommand::from_layers(before, paint, dirty, "Pencil stroke"));
    errors += expect(clean->dirty(), "stroke marks unsaved");
    clean->undo();
    errors += expect(!clean->dirty(), "undo to the initial image clears unsaved");
    clean->redo();
    errors += expect(clean->dirty(), "redo away from the initial image is unsaved");
    clean->mark_clean();
    errors += expect(!clean->dirty(), "save clears unsaved");
    const int saved = clean->history().index();
    clean->undo();
    errors += expect(clean->dirty(), "undo away from the save is unsaved");
    clean->redo();
    errors += expect(!clean->dirty(), "redo back to the save clears unsaved");
    errors += expect(clean->history().index() == saved, "redo landed on the save");

    Layer before2(paint.width(), paint.height(), Color::transparent(), "before2");
    before2.copy_from(paint);
    Rect dirty2{};
    lundukepaint::stroke_pencil(paint.pixels(), paint.width(), paint.height(), paint.stride(), 1, 1, 3,
                            1, 1, Color{0, 0, 255, 255}, &dirty2);
    clean->commit(PixelPatchCommand::from_layers(before2, paint, dirty2, "Second stroke"));
    errors += expect(clean->dirty(), "new stroke after save is unsaved");
    clean->jump_history(saved);
    errors += expect(!clean->dirty(), "jump back to the save clears unsaved");
    clean->jump_history(clean->history().count() - 1);
    errors += expect(clean->dirty(), "jump away from the save is unsaved");
  }

  {
    auto doc = Document::create(4, 3, Color::transparent(), "Lower");
    Layer& lower = doc->layers().active_layer();
    std::vector<std::uint8_t> small(2 * 2 * 4, 0);
    small[0] = 255;
    small[3] = 255;
    lower.set_pixels(2, 2, small.data(), 8);
    lower.set_opacity(0.5f);
    lower.set_blend(lundukepaint::BlendMode::Multiply);
    lower.set_offset(1, 1);
    doc->add_layer();
    Layer& upper = doc->layers().active_layer();
    upper.set_pixel(0, 0, Color{0, 0, 255, 255});
    std::vector<std::uint8_t> before(static_cast<std::size_t>(4 * 3 * 4), 0);
    doc->layers().composite_rect(before.data(), 16, Rect{0, 0, 4, 3});
    errors += expect(doc->merge_down(), "merge down");
    std::vector<std::uint8_t> after(static_cast<std::size_t>(4 * 3 * 4), 0);
    doc->layers().composite_rect(after.data(), 16, Rect{0, 0, 4, 3});
    errors += expect(before == after, "merge matches the screen");
    errors += expect(doc->layers().count() == 1, "merge leaves one layer");
    const Layer& merged = doc->layers().active_layer();
    errors += expect(merged.opacity() > 0.99f, "merged opacity is opaque");
    errors += expect(merged.blend() == lundukepaint::BlendMode::Normal, "merged blend is normal");
    errors += expect(merged.offset_x() == 0 && merged.offset_y() == 0, "merged offset is rebased");
    errors += expect(merged.width() == 4 && merged.height() == 3, "merged buffer is the canvas");
  }

  {
    auto doc = Document::create(4, 3, Color::transparent(), "Lower");
    Layer& lower = doc->layers().active_layer();
    std::vector<std::uint8_t> red(4, 0);
    red[0] = 255;
    red[3] = 255;
    lower.set_pixels(1, 1, red.data(), 4);
    lower.set_opacity(0.5f);
    lower.set_offset(1, 1);
    doc->add_layer();
    Layer& upper = doc->layers().active_layer();
    std::vector<std::uint8_t> dot(4, 0);
    dot[2] = 255;
    dot[3] = 255;
    upper.set_pixels(1, 1, dot.data(), 4);
    upper.set_offset(2, 2);
    errors += expect(doc->merge_down(), "normal merge down");
    const Layer& merged = doc->layers().active_layer();
    errors += expect(merged.blend() == lundukepaint::BlendMode::Normal, "normal merge keeps lower blend");
    errors += expect(merged.opacity() > 0.49f && merged.opacity() < 0.51f, "normal merge keeps opacity");
    errors += expect(merged.offset_x() == 1 && merged.offset_y() == 1, "normal merge keeps union origin");
    errors += expect(merged.width() == 2 && merged.height() == 2, "normal merge is the union");
  }

  {
    auto doc = Document::create(2, 1, Color::white(), "Background");
    doc->layers().active_layer().set_pixel(0, 0, Color{255, 0, 0, 255});
    lundukepaint::StackXform xform;
    xform.kind = lundukepaint::StackXformKind::FlipH;
    xform.old_w = 2;
    xform.old_h = 1;
    xform.new_w = 2;
    xform.new_h = 1;
    auto cmd = std::make_unique<lundukepaint::AllLayersBufferCommand>(
        "Flip", doc->snapshot_layers(), 0, xform);
    doc->commit(std::move(cmd));
    errors += expect(doc->layers().active_layer().pixel(1, 0) == (Color{255, 0, 0, 255}),
                     "flip redo via parameters");
    doc->undo();
    errors += expect(doc->layers().active_layer().pixel(0, 0) == (Color{255, 0, 0, 255}),
                     "flip undo restores pre-image");
    doc->redo();
    errors += expect(doc->layers().active_layer().pixel(1, 0) == (Color{255, 0, 0, 255}),
                     "flip redo matches");
  }

  auto commit_dot = [](Document& doc, int x, Color color) {
    Layer& layer = doc.layers().active_layer();
    Layer before(layer.width(), layer.height(), Color::transparent(), "before");
    before.copy_from(layer);
    layer.set_pixel(x, 0, color);
    Layer after(layer.width(), layer.height(), Color::transparent(), "after");
    after.copy_from(layer);
    doc.commit(PixelPatchCommand::from_layers(before, after, Rect{x, 0, 1, 1}, "Dot"));
  };

  // Depth 2 drops the oldest stroke. The saved checkpoint was the initial
  // image (index -1), so that base is no longer the file on disk.
  {
    auto trimmed = Document::create(8, 2, Color::white(), "Background");
    trimmed->mark_clean();
    trimmed->history().set_depth(2);
    commit_dot(*trimmed, 0, Color{255, 0, 0, 255});
    commit_dot(*trimmed, 1, Color{0, 255, 0, 255});
    commit_dot(*trimmed, 2, Color{0, 0, 255, 255});
    trimmed->jump_history(-1);
    errors += expect(trimmed->layers().active_layer().pixel(0, 0) == (Color{255, 0, 0, 255}),
                     "trimmed base keeps the baked stroke");
    errors += expect(trimmed->dirty(), "undo to trimmed base stays dirty");
    errors += expect(!trimmed->history().matches_saved(), "trimmed base is not the saved image");
  }
  {
    auto trimmed = Document::create(8, 2, Color::white(), "Background");
    errors += expect(trimmed->history().index() == -1, "fresh history is the initial image");
    trimmed->mark_clean();
    errors += expect(trimmed->history().matches_saved(), "mark_clean at -1 matches");
    trimmed->history().set_depth(2);
    commit_dot(*trimmed, 0, Color{255, 0, 0, 255});
    commit_dot(*trimmed, 1, Color{0, 255, 0, 255});
    commit_dot(*trimmed, 2, Color{0, 0, 255, 255});
    trimmed->jump_history(-1);
    errors += expect(trimmed->dirty() && !trimmed->history().matches_saved(),
                     "mark_clean at -1 then trim still disagrees with the file");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_history: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_history: ok\n");
  return 0;
}
