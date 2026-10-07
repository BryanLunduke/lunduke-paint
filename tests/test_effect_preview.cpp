// SPDX-License-Identifier: GPL-3.0-or-later
//
// Live-preview semantics for the adjustment dialogs (Brightness/Contrast,
// Hue/Saturation, Blur, Posterize): preview never touches history or the dirty
// flag, Cancel restores the original pixels byte for byte, and OK lands exactly
// one undoable command whose result matches the preview.

#include "doc/document.hpp"
#include "doc/effect_preview.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "io/crash_recovery.hpp"
#include "raster/effects.hpp"

#include <cstring>

#include <cstdio>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::EffectPreview;
using lundukepaint::Layer;
using lundukepaint::Rect;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_effect_preview: %s\n", msg);
    ++errors;
  }
}

std::vector<std::uint8_t> dump(const Layer& layer) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(layer.stride()) *
                                static_cast<std::size_t>(layer.height()));
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = layer.pixels()[i];
  }
  return out;
}

EffectPreview::EffectFn brightness(int amount) {
  return [amount](std::uint8_t* px, int w, int h, int stride) {
    lundukepaint::brightness_contrast_rgba(px, w, h, stride, amount, 0);
  };
}

std::unique_ptr<Document> make_doc() {
  auto doc = Document::create(8, 6, Color{100, 110, 120, 255}, "Background");
  Layer& layer = doc->layers().active_layer();
  layer.set_pixel(0, 0, Color{10, 20, 30, 255});
  layer.set_pixel(7, 5, Color{200, 210, 220, 255});
  doc->mark_clean();
  return doc;
}

}  // namespace

int main() {
  // 1. Cancel after a live preview restores the exact original pixels and
  //    leaves history and the dirty flag untouched.
  {
    auto doc = make_doc();
    const std::vector<std::uint8_t> original = dump(doc->layers().active_layer());
    EffectPreview effect(*doc);
    expect(effect.valid(), "snapshot taken");
    effect.preview(brightness(40));
    expect(effect.previewing(), "preview marked active");
    expect(dump(doc->layers().active_layer()) != original, "preview changed pixels");
    expect(doc->history().count() == 0, "preview pushed no history");
    expect(!doc->dirty(), "preview left the document clean");
    expect(effect.restore(), "restore reported work");
    expect(dump(doc->layers().active_layer()) == original, "cancel restored pixels exactly");
    expect(doc->history().count() == 0, "cancel pushed no history");
    expect(!doc->dirty(), "cancel left the document clean");
    expect(!effect.restore(), "second restore is a no-op");
  }

  // 2. Dragging the slider re-previews from the snapshot instead of stacking
  //    effect on top of effect.
  {
    auto doc = make_doc();
    EffectPreview effect(*doc);
    effect.preview(brightness(10));
    effect.preview(brightness(10));
    const std::vector<std::uint8_t> twice = dump(doc->layers().active_layer());

    auto other = make_doc();
    EffectPreview single(*other);
    single.preview(brightness(10));
    expect(twice == dump(other->layers().active_layer()),
           "repeated preview does not stack the effect");
  }

  // 3. OK after a live preview commits once, matches the preview, and undoes
  //    back to the original in a single step.
  {
    auto doc = make_doc();
    const std::vector<std::uint8_t> original = dump(doc->layers().active_layer());
    EffectPreview effect(*doc);
    effect.preview(brightness(40));
    const std::vector<std::uint8_t> previewed = dump(doc->layers().active_layer());
    expect(effect.commit("Brightness / Contrast", brightness(40)), "commit pushed a command");
    expect(doc->history().count() == 1, "exactly one history entry");
    expect(doc->history().name_at(0) == "Brightness / Contrast", "history entry is named");
    expect(dump(doc->layers().active_layer()) == previewed,
           "committed pixels match the preview (no double apply)");
    expect(doc->dirty(), "commit marks the document dirty");
    doc->undo();
    expect(dump(doc->layers().active_layer()) == original, "single undo returns to the original");
    expect(!doc->history().can_undo(), "history had only one entry");
    doc->redo();
    expect(dump(doc->layers().active_layer()) == previewed, "redo re-applies the effect");
  }

  // 4. Live preview off: commit without any preview behaves exactly like the
  //    old single-apply path.
  {
    auto doc = make_doc();
    auto reference = make_doc();
    EffectPreview effect(*doc);
    expect(effect.commit("Blur", [](std::uint8_t* px, int w, int h, int stride) {
             std::vector<std::uint8_t> src(static_cast<std::size_t>(h) *
                                           static_cast<std::size_t>(stride));
             for (std::size_t i = 0; i < src.size(); ++i) {
               src[i] = px[i];
             }
             lundukepaint::box_blur_rgba(src.data(), w, h, stride, px, stride, 2);
           }),
           "blur committed without a preview");
    expect(doc->history().count() == 1, "one entry for the un-previewed apply");

    Layer& ref_layer = reference->layers().active_layer();
    std::vector<std::uint8_t> src(static_cast<std::size_t>(ref_layer.stride()) *
                                  static_cast<std::size_t>(ref_layer.height()));
    for (std::size_t i = 0; i < src.size(); ++i) {
      src[i] = ref_layer.pixels()[i];
    }
    std::vector<std::uint8_t> out(src.size());
    lundukepaint::box_blur_rgba(src.data(), ref_layer.width(), ref_layer.height(), ref_layer.stride(),
                            out.data(), ref_layer.stride(), 2);
    ref_layer.write_rect(Rect{0, 0, ref_layer.width(), ref_layer.height()}, out.data());
    expect(dump(doc->layers().active_layer()) == dump(ref_layer),
           "un-previewed commit matches a plain single apply");
  }

  // 5. A no-op effect commits nothing at all.
  {
    auto doc = make_doc();
    EffectPreview effect(*doc);
    expect(!effect.commit("Brightness / Contrast", brightness(0)), "zero adjustment commits nothing");
    expect(doc->history().count() == 0, "no history for a no-op");
    expect(!doc->dirty(), "no-op leaves the document clean");
  }

  // 6. Preview honours the selection: pixels outside it never change.
  {
    auto doc = make_doc();
    doc->selection().set_rect(Rect{0, 0, 4, 6});
    const Color outside_before = doc->layers().active_layer().pixel(7, 0);
    EffectPreview effect(*doc);
    effect.preview(brightness(60));
    expect(doc->layers().active_layer().pixel(7, 0) == outside_before,
           "preview leaves pixels outside the selection alone");
    expect(doc->layers().active_layer().pixel(1, 1) != outside_before,
           "preview changes pixels inside the selection");
    effect.commit("Brightness / Contrast", brightness(60));
    expect(doc->layers().active_layer().pixel(7, 0) == outside_before,
           "commit leaves pixels outside the selection alone");
    expect(doc->history().count() == 1, "selection commit is one entry");
  }

  // Wider than 2048: preview and commit run the same full-resolution blur.
  {
    auto doc = Document::create(2049, 2, Color::black(), "Wide");
    Layer& layer = doc->layers().active_layer();
    for (int x = 0; x < 1024; ++x) {
      layer.set_pixel(x, 0, Color::white());
      layer.set_pixel(x, 1, Color::white());
    }
    auto blur = [](std::uint8_t* rgba, int width, int height, int stride) {
      std::vector<std::uint8_t> src(static_cast<std::size_t>(stride) * static_cast<std::size_t>(height));
      std::memcpy(src.data(), rgba, src.size());
      lundukepaint::box_blur_rgba(src.data(), width, height, stride, rgba, stride, 1);
    };
    EffectPreview effect(*doc);
    effect.preview(blur);
    expect(effect.previewing(), "wide preview is installed");
    expect(!lundukepaint::crash_recovery::recovery_snapshot_allowed(true),
           "a live preview blocks a recovery snapshot");
    expect(!lundukepaint::crash_recovery::recovery_snapshot_allowed(effect.previewing()),
           "previewing() blocks a recovery snapshot");
    const std::vector<std::uint8_t> previewed = dump(doc->layers().active_layer());
    effect.restore();
    expect(effect.commit("Blur", blur), "wide blur commits");
    expect(dump(doc->layers().active_layer()) == previewed, "wide preview matches commit");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_effect_preview: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_effect_preview: ok\n");
  return 0;
}
