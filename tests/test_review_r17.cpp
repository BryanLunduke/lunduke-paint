// SPDX-License-Identifier: GPL-3.0-or-later
// Second 1.1.0 review: GIF cancel and exact transparency, free-rotate undo,
// delete after rotate, selection-bounded effects, paper detection, and
// brush remap.

#include "doc/document.hpp"
#include "doc/effect_preview.hpp"
#include "doc/layer.hpp"
#include "doc/selection.hpp"
#include "io/image_io.hpp"
#include "raster/custom_brush.hpp"
#include "raster/effects.hpp"
#include "raster/transform.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::CustomBrush;
using lundukepaint::Document;
using lundukepaint::EffectPreview;
using lundukepaint::HalftoneScreen;
using lundukepaint::ImageFormat;
using lundukepaint::Layer;
using lundukepaint::WindDirection;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "FAIL %s\n", msg);
    ++errors;
  }
}

Color px_at(const std::uint8_t* rgba, int stride, int x, int y) {
  const std::uint8_t* p = rgba + static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

void fill(Layer& layer, Color color) {
  for (int y = 0; y < layer.height(); ++y) {
    for (int x = 0; x < layer.width(); ++x) {
      layer.set_pixel(x, y, color);
    }
  }
}

void test_save_policy() {
  expect(!lundukepaint::flat_save_marks_clean(ImageFormat::Gif, true),
         "GIF export does not mark the document clean");
  expect(!lundukepaint::flat_save_marks_clean(ImageFormat::Gif, false),
         "a changed GIF export stays dirty");
  expect(lundukepaint::flat_save_marks_clean(ImageFormat::Png, true),
         "an unchanged PNG save marks clean");
  expect(!lundukepaint::flat_save_marks_clean(ImageFormat::Png, false),
         "an edit after the snapshot stays dirty");
  expect(std::strcmp(lundukepaint::closed_during_save_detail(true), "The file was written.") == 0,
         "closed tab says the file was written");
  expect(std::strcmp(lundukepaint::closed_during_save_detail(false), "Nothing was written.") == 0,
         "closed tab before the write says nothing was written");

  auto doc = Document::create(4, 4, Color::white());
  const auto serial = doc->edit_serial();
  doc->notify_invalidated({0, 0, 1, 1});
  expect(doc->edit_serial() != serial, "a pixel invalidation moves the edit serial");
}

void test_gif_cancel_and_exact() {
  std::vector<std::uint8_t> rgba(8 * 8 * 4, 255);
  std::vector<std::uint8_t> encoded;
  std::string error;
  int calls = 0;
  const bool aborted = lundukepaint::encode_gif_memory(
      rgba.data(), 8, 8, 32, encoded, error,
      [&calls](int, int) {
        ++calls;
        return false;
      });
  expect(!aborted && error == "cancelled" && encoded.empty() && calls > 0,
         "a cancel stops the GIF encoder before any bytes");

  const std::string path = std::string("/tmp/lunduke-r17-cancel-") + std::to_string(getpid()) + ".gif";
  std::string save_error;
  const bool wrote = lundukepaint::save_flat_image(
      path, ImageFormat::Gif, rgba.data(), 8, 8, 32, 90, save_error,
      [](int, int) { return false; });
  expect(!wrote && save_error == "cancelled", "a cancelled save reports cancelled");
  expect(!lundukepaint::load_flat_image(path).ok(), "a cancelled save writes no file");
  std::remove(path.c_str());

  // 255 opaque colors plus one clear pixel stay exact.
  const int w = 16;
  const int h = 16;
  std::vector<std::uint8_t> exact(static_cast<std::size_t>(w * h * 4), 0);
  int n = 0;
  for (int y = 0; y < h && n < 255; ++y) {
    for (int x = 0; x < w && n < 255; ++x) {
      Color c{static_cast<std::uint8_t>(n), static_cast<std::uint8_t>(40 + (n % 50)),
              static_cast<std::uint8_t>(80 + (n % 70)), 255};
      if (n == 0) {
        c = Color::transparent();
      }
      std::uint8_t* p = exact.data() + (static_cast<std::size_t>(y) * w + x) * 4;
      p[0] = c.r;
      p[1] = c.g;
      p[2] = c.b;
      p[3] = c.a;
      ++n;
    }
  }
  encoded.clear();
  error.clear();
  expect(lundukepaint::encode_gif_memory(exact.data(), w, h, w * 4, encoded, error),
         "255 opaque colors plus alpha encode");
  const std::string gif = std::string("/tmp/lunduke-r17-exact-") + std::to_string(getpid()) + ".gif";
  FILE* out = std::fopen(gif.c_str(), "wb");
  expect(out != nullptr, "open exact gif");
  if (out != nullptr) {
    expect(std::fwrite(encoded.data(), 1, encoded.size(), out) == encoded.size(), "write exact gif");
    std::fclose(out);
  }
  lundukepaint::LoadedImage back = lundukepaint::load_flat_image(gif);
  expect(back.ok(), "decode 255-color gif");
  if (back.ok()) {
    expect(px_at(back.rgba.data(), back.width * 4, 0, 0).a == 0, "clear pixel stays clear");
    const Color want = px_at(exact.data(), w * 4, 3, 2);
    const Color got = px_at(back.rgba.data(), back.width * 4, 3, 2);
    expect(want == got, "an opaque color under the limit is exact");
  }
  std::remove(gif.c_str());

  // 256 opaque colors plus one clear pixel: only the nearest pair may move.
  const int mw = 17;
  const int mh = 16;
  std::vector<std::uint8_t> packed(static_cast<std::size_t>(mw * mh * 4), 0);
  for (int i = 0; i < 256; ++i) {
    const int x = (i + 1) % mw;
    const int y = (i + 1) / mw;
    const Color c{static_cast<std::uint8_t>(i), 20, 90, 255};
    std::uint8_t* p = packed.data() + (static_cast<std::size_t>(y) * mw + x) * 4;
    p[0] = c.r;
    p[1] = c.g;
    p[2] = c.b;
    p[3] = c.a;
  }
  encoded.clear();
  error.clear();
  expect(lundukepaint::encode_gif_memory(packed.data(), mw, mh, mw * 4, encoded, error),
         "256 opaque colors plus alpha encode");
  const std::string gif2 = std::string("/tmp/lunduke-r17-merge-") + std::to_string(getpid()) + ".gif";
  out = std::fopen(gif2.c_str(), "wb");
  if (out != nullptr) {
    std::fwrite(encoded.data(), 1, encoded.size(), out);
    std::fclose(out);
  }
  back = lundukepaint::load_flat_image(gif2);
  expect(back.ok(), "decode merged gif");
  if (back.ok()) {
    expect(px_at(back.rgba.data(), back.width * 4, 0, 0).a == 0, "merged gif keeps transparency");
    int shifted = 0;
    int worst = 0;
    for (int i = 0; i < 256; ++i) {
      const int x = (i + 1) % mw;
      const int y = (i + 1) / mw;
      const Color want = px_at(packed.data(), mw * 4, x, y);
      const Color got = px_at(back.rgba.data(), back.width * 4, x, y);
      const int d = std::max(std::max(std::abs(static_cast<int>(want.r) - got.r),
                                      std::abs(static_cast<int>(want.g) - got.g)),
                             std::abs(static_cast<int>(want.b) - got.b));
      if (d != 0) {
        ++shifted;
        worst = std::max(worst, d);
      }
    }
    expect(shifted <= 1, "only the merged color moves");
    expect(worst <= 1, "the merged pair was the nearest colors");
  }
  std::remove(gif2.c_str());
}

void rotate_float(Document& doc, double degrees) {
  auto& sel = doc.selection();
  auto turned = lundukepaint::rotate_nearest_angle(sel.float_pixels(), sel.float_w(), sel.float_h(),
                                                   sel.float_w() * 4, sel.float_coverage(), degrees);
  const int nx = sel.float_x();
  const int ny = sel.float_y();
  sel.transform_float(nx, ny, turned.width, turned.height, std::move(turned.rgba),
                      std::move(turned.coverage));
}

void test_second_rotate_and_delete() {
  auto doc = Document::create(16, 16, Color::white());
  Layer& layer = doc->layers().active_layer();
  layer.set_pixel(4, 4, {10, 20, 30, 255});
  layer.set_pixel(6, 5, {40, 50, 60, 255});
  doc->selection().set_rect({4, 4, 4, 4});
  const auto original = doc->selection().capture();
  expect(doc->selection().lift(layer, 0), "lift");
  rotate_float(*doc, 90);
  doc->arm_pending_transform("Free rotate", original);
  rotate_float(*doc, 15);
  doc->arm_pending_transform("Free rotate", doc->selection().capture());
  expect(doc->try_commit_floating("Move selection"), "place the second rotate");
  expect(!doc->selection().floating(), "place drops the float");
  expect(doc->history().can_undo(), "place is one step");
  doc->undo();
  expect(!doc->selection().floating(), "one undo after two rotates is not a live float");
  expect(layer.pixel(4, 4) == (Color{10, 20, 30, 255}), "undo restores the pre-rotate pixel");
  expect(doc->selection().bounds().x == 4 && doc->selection().bounds().w == 4,
         "undo restores the original selection");
  expect(!doc->history().can_undo(), "there is no second undo into the float");

  expect(doc->selection().lift(layer, 0), "lift for delete");
  rotate_float(*doc, 90);
  doc->arm_pending_transform("Free rotate", original);
  doc->delete_selection();
  expect(!doc->pending_transform(), "delete discards the pending rotate");
  expect(!doc->selection().floating(), "delete drops the float");
  expect(layer.pixel(4, 4).a == 0, "delete punches the origin");
  expect(doc->history().can_undo(), "delete is a history step");
  doc->undo();
  expect(!doc->pending_transform(), "undo does not re-arm the rotate");
  expect(!doc->selection().floating(), "undo does not bring the float back");
  expect(layer.pixel(4, 4) == (Color{10, 20, 30, 255}), "undo restores the deleted pixel");
  expect(doc->selection().bounds().w == 4, "undo restores the resting selection");
}

void test_selection_effects() {
  auto doc = Document::create(16, 16, Color::white());
  Layer& layer = doc->layers().active_layer();
  fill(layer, {0, 0, 0, 255});
  doc->selection().set_rect({2, 2, 12, 12});
  EffectPreview trace(*doc);
  const bool traced = trace.commit("Trace edges", [](std::uint8_t* px, int w, int h, int stride) {
    lundukepaint::trace_edges_rgba(px, w, h, stride, Color{255, 0, 0, 255}, Color::white());
  });
  expect(traced, "trace commits");
  bool outlined = false;
  for (int y = 2; y < 14 && !outlined; ++y) {
    for (int x = 2; x < 14; ++x) {
      const Color c = layer.pixel(x, y);
      if (c.r == 255 && c.g == 0 && c.b == 0) {
        outlined = true;
        break;
      }
    }
  }
  expect(!outlined, "trace edges does not outline the selection");
  expect(layer.pixel(0, 0) == (Color{0, 0, 0, 255}), "trace leaves pixels outside the selection");

  auto tone = Document::create(16, 16, Color::white());
  Layer& tone_layer = tone->layers().active_layer();
  fill(tone_layer, {128, 128, 128, 255});
  std::vector<std::uint8_t> reference(tone_layer.pixels(), tone_layer.pixels() + 16 * 16 * 4);
  lundukepaint::halftone_rgba(reference.data(), 16, 16, 64, Color::black(), Color::white(), 4,
                              HalftoneScreen::Bayer);
  tone->selection().set_rect({8, 4, 8, 8});
  EffectPreview half(*tone);
  expect(half.commit("Halftone", [](std::uint8_t* px, int w, int h, int stride) {
           lundukepaint::halftone_rgba(px, w, h, stride, Color::black(), Color::white(), 4,
                                       HalftoneScreen::Bayer);
         }),
         "halftone commits on a selection");
  bool seam = false;
  for (int y = 4; y < 12 && !seam; ++y) {
    for (int x = 8; x < 16; ++x) {
      if (tone_layer.pixel(x, y) != px_at(reference.data(), 64, x, y)) {
        seam = true;
      }
    }
  }
  expect(!seam, "halftone phase matches the full image inside the selection");
  expect(tone_layer.pixel(0, 0) == (Color{128, 128, 128, 255}), "halftone leaves the unselected corner");

  auto blur_doc = Document::create(12, 12, Color::white());
  Layer& blur_layer = blur_doc->layers().active_layer();
  fill(blur_layer, Color::white());
  for (int y = 0; y < 12; ++y) {
    blur_layer.set_pixel(0, y, Color::black());
  }
  blur_doc->selection().set_rect({1, 2, 8, 8});
  EffectPreview blur(*blur_doc);
  expect(blur.commit("Blur", [](std::uint8_t* px, int w, int h, int stride) {
           std::vector<std::uint8_t> src(px, px + static_cast<std::size_t>(h) * stride);
           lundukepaint::box_blur_rgba(src.data(), w, h, stride, px, stride, 1);
         }),
         "blur commits");
  const Color beside = blur_layer.pixel(1, 4);
  expect(beside.r < 250, "blur samples the black neighbor outside the selection");
  expect(blur_layer.pixel(0, 0) == Color::black(), "blur does not write outside the selection");

  auto lines = Document::create(8, 8, Color::white());
  Layer& line_layer = lines->layers().active_layer();
  fill(line_layer, {180, 180, 180, 255});
  std::vector<std::uint8_t> crt_ref(line_layer.pixels(), line_layer.pixels() + 8 * 8 * 4);
  lundukepaint::crt_scanlines_rgba(crt_ref.data(), 8, 8, 32, 100);
  lines->selection().set_rect({0, 1, 8, 4});
  EffectPreview scan(*lines);
  expect(scan.commit("CRT", [](std::uint8_t* px, int w, int h, int stride) {
           lundukepaint::crt_scanlines_rgba(px, w, h, stride, 100);
         }),
         "scanlines commit");
  expect(line_layer.pixel(2, 1) == px_at(crt_ref.data(), 32, 2, 1),
         "scanlines keep the full-image row phase inside the selection");
  expect(line_layer.pixel(2, 0) == (Color{180, 180, 180, 255}), "scanlines leave the unselected row");
}

void test_paper_and_remap() {
  std::vector<std::uint8_t> white(8 * 8 * 4, 255);
  const Color paper = lundukepaint::infer_paper(white.data(), 8, 8, 32);
  expect(paper == Color::white(), "white paper is detected from the pixels");

  std::vector<std::uint8_t> clear(8 * 8 * 4, 0);
  const Color empty = lundukepaint::infer_paper(clear.data(), 8, 8, 32);
  expect(empty.a == 0, "a clear layer is transparent paper");

  std::vector<std::uint8_t> sheet(16 * 16 * 4, 255);
  for (int y = 4; y < 12; ++y) {
    for (int x = 4; x < 12; ++x) {
      std::uint8_t* p = sheet.data() + (static_cast<std::size_t>(y) * 16 + x) * 4;
      p[0] = p[1] = p[2] = 0;
      p[3] = 255;
    }
  }
  expect(lundukepaint::infer_paper(sheet.data(), 16, 16, 64) == Color::white(),
         "a black mark on white still reads as white paper");
  std::vector<std::uint8_t> as_white = sheet;
  std::vector<std::uint8_t> as_black = sheet;
  lundukepaint::wind_rgba(as_white.data(), 16, 16, 64, WindDirection::Right, 4, 1, Color::white());
  lundukepaint::wind_rgba(as_black.data(), 16, 16, 64, WindDirection::Right, 4, 1, Color::black());
  std::vector<std::uint8_t> as_inferred = sheet;
  const Color detected = lundukepaint::infer_paper(as_inferred.data(), 16, 16, 64);
  lundukepaint::wind_rgba(as_inferred.data(), 16, 16, 64, WindDirection::Right, 4, 1, detected);
  expect(as_inferred == as_white, "wind follows the detected paper, not a black swatch");
  expect(as_black != as_white, "a black paper swatch really does invert wind");

  CustomBrush brush;
  brush.width = 2;
  brush.height = 1;
  brush.rgba = {10, 20, 30, 255, 0, 0, 0, 0};
  const CustomBrush before = brush;
  lundukepaint::custom_brush_remap(brush, Color::transparent());
  expect(brush.rgba == before.rgba, "remap ignores a transparent foreground");
  lundukepaint::custom_brush_remap(brush, {1, 2, 3, 255});
  expect(brush.rgba[0] == 1 && brush.rgba[1] == 2 && brush.rgba[2] == 3 && brush.rgba[3] == 255,
         "remap still recolors an opaque foreground");
  expect(brush.rgba[7] == 0, "remap leaves a clear pixel clear");
}

}  // namespace

int main() {
  test_save_policy();
  test_gif_cancel_and_exact();
  test_second_rotate_and_delete();
  test_selection_effects();
  test_paper_and_remap();
  if (errors != 0) {
    std::fprintf(stderr, "test_review_r17: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_review_r17: ok\n");
  return 0;
}
