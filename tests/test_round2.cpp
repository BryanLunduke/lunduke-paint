// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "io/image_io.hpp"
#include "raster/brush_tip.hpp"
#include "raster/custom_brush.hpp"
#include "raster/effects.hpp"
#include "raster/shapes.hpp"
#include "raster/transform.hpp"
#include "ui/colors_panel.hpp"
#include "ui/rail_sizes.hpp"

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::CustomBrush;
using lundukepaint::Document;
using lundukepaint::Selection;
using lundukepaint::SelectionCommand;
using lundukepaint::SelectionMorph;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_round2: %s\n", msg);
    ++errors;
  }
}

Color get(const std::uint8_t* buf, int stride, int x, int y) {
  const std::uint8_t* p = buf + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                          static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

void setpx(std::uint8_t* buf, int stride, int x, int y, Color c) {
  std::uint8_t* p = buf + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                    static_cast<std::size_t>(x) * 4;
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

void fill(std::vector<std::uint8_t>& buf, int w, int h, Color c) {
  buf.assign(static_cast<std::size_t>(w) * h * 4, 0);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      setpx(buf.data(), w * 4, x, y, c);
    }
  }
}

void test_halftone() {
  const int w = 8;
  const int h = 8;
  std::vector<std::uint8_t> buf;
  fill(buf, w, h, Color{128, 128, 128, 255});
  setpx(buf.data(), w * 4, 0, 0, Color{0, 0, 0, 255});
  setpx(buf.data(), w * 4, 1, 0, Color{255, 255, 255, 200});
  const Color ink{0, 0, 0, 255};
  const Color paper{255, 255, 255, 255};
  lundukepaint::halftone_rgba(buf.data(), w, h, w * 4, ink, paper, 1, lundukepaint::HalftoneScreen::Bayer);
  expect(get(buf.data(), w * 4, 0, 0) == ink, "black is ink");
  expect(get(buf.data(), w * 4, 1, 0) == paper, "white is paper");
  expect(get(buf.data(), w * 4, 1, 0).a == 255, "paper replaces alpha");
  bool only = true;
  for (int i = 0; i < w * h; ++i) {
    const Color c = get(buf.data(), w * 4, i % w, i / w);
    if (c != ink && c != paper) {
      only = false;
    }
  }
  expect(only, "halftone is only ink or paper");

  fill(buf, 4, 4, Color{40, 40, 40, 255});
  lundukepaint::halftone_rgba(buf.data(), 4, 4, 16, ink, paper, 2, lundukepaint::HalftoneScreen::Clustered);
  expect(get(buf.data(), 16, 0, 0) == get(buf.data(), 16, 1, 0), "a cell of 2 shares a threshold");
  expect(get(buf.data(), 16, 0, 0) == ink || get(buf.data(), 16, 0, 0) == paper, "clustered is hard");
}

void test_diffuse() {
  std::vector<std::uint8_t> buf;
  fill(buf, 6, 4, Color::white());
  setpx(buf.data(), 24, 1, 1, Color{1, 2, 3, 255});
  setpx(buf.data(), 24, 4, 2, Color{9, 8, 7, 255});
  auto copy = buf;
  lundukepaint::diffuse_rgba(buf.data(), 6, 4, 24, 0, 1);
  expect(buf == copy, "radius 0 does not move pixels");
  lundukepaint::diffuse_rgba(buf.data(), 6, 4, 24, 2, 99);
  auto again = copy;
  lundukepaint::diffuse_rgba(again.data(), 6, 4, 24, 2, 99);
  expect(buf == again, "diffuse repeats with the same seed");
  std::map<std::uint32_t, int> before;
  std::map<std::uint32_t, int> after;
  auto key = [](Color c) {
    return (static_cast<std::uint32_t>(c.r) << 24) | (static_cast<std::uint32_t>(c.g) << 16) |
           (static_cast<std::uint32_t>(c.b) << 8) | c.a;
  };
  for (int y = 0; y < 4; ++y) {
    for (int x = 0; x < 6; ++x) {
      ++before[key(get(copy.data(), 24, x, y))];
      ++after[key(get(buf.data(), 24, x, y))];
    }
  }
  expect(before == after, "diffuse only swaps pixels");
}

void test_shadow_wind_crt() {
  std::vector<std::uint8_t> buf;
  fill(buf, 8, 8, Color::white());
  setpx(buf.data(), 32, 1, 1, Color::black());
  lundukepaint::drop_shadow_rgba(buf.data(), 8, 8, 32, 2, 3, Color{255, 0, 0, 255}, Color::white());
  expect(get(buf.data(), 32, 1, 1) == Color::black(), "the mark stays");
  expect(get(buf.data(), 32, 3, 4) == (Color{255, 0, 0, 255}), "shadow is the offset background color");
  expect(get(buf.data(), 32, 2, 2) == Color::white(), "a nearer pixel is not shaded");

  fill(buf, 10, 3, Color::white());
  setpx(buf.data(), 40, 0, 1, Color::black());
  lundukepaint::wind_rgba(buf.data(), 10, 3, 40, lundukepaint::WindDirection::Right, 4, 1);
  expect(get(buf.data(), 40, 0, 1) == Color::black(), "wind keeps the source pixel");
  int streak = 0;
  for (int x = 1; x < 8; ++x) {
    if (get(buf.data(), 40, x, 1) == Color::black()) {
      ++streak;
    }
  }
  expect(streak >= 1, "wind copies the edge to the right");

  fill(buf, 2, 4, Color{200, 100, 50, 180});
  lundukepaint::crt_scanlines_rgba(buf.data(), 2, 4, 8, 50);
  expect(get(buf.data(), 8, 0, 0) == (Color{200, 100, 50, 180}), "even rows stay");
  const Color odd = get(buf.data(), 8, 0, 1);
  expect(odd.r < 200 && odd.g < 100 && odd.b < 50, "odd rows darken");
  expect(odd.a == 180, "scanlines keep alpha");
  expect(get(buf.data(), 8, 1, 2) == (Color{200, 100, 50, 180}), "the next even row stays");
}

void test_selection_morph() {
  auto doc = Document::create(16, 12, Color::white());
  doc->selection().set_rect({2, 2, 4, 4});
  expect(lundukepaint::morph_selection(doc->selection(), 16, 12, SelectionMorph::Expand, 1), "expand");
  expect(doc->selection().bounds().x == 1 && doc->selection().bounds().y == 1, "expand origin");
  expect(doc->selection().bounds().w == 6 && doc->selection().bounds().h == 6, "expand size");
  expect(doc->selection().contains(1, 1) && doc->selection().contains(6, 6), "expand corners");

  doc->selection().set_rect({2, 2, 6, 6});
  const auto before = doc->selection().capture();
  lundukepaint::morph_selection(doc->selection(), 16, 12, SelectionMorph::Contract, 1);
  expect(doc->selection().bounds().x == 3 && doc->selection().bounds().w == 4, "contract");
  doc->commit(std::make_unique<SelectionCommand>("Shrink selection", before, doc->selection().capture()));
  expect(doc->history().can_undo(), "shrink is undoable");
  doc->undo();
  expect(doc->selection().bounds().w == 6, "undo restores the selection");

  doc->selection().set_rect({4, 4, 5, 5});
  lundukepaint::morph_selection(doc->selection(), 16, 12, SelectionMorph::Border, 1);
  expect(doc->selection().contains(4, 4), "border keeps the edge");
  expect(!doc->selection().contains(6, 6), "border drops the interior");
  expect(doc->selection().contains(3, 4), "border grows one pixel outside");
}

void test_rotate_shear() {
  const Color a{10, 0, 0, 255};
  const Color b{0, 20, 0, 255};
  const Color c{0, 0, 30, 255};
  const Color d{40, 40, 0, 255};
  std::vector<std::uint8_t> src(16, 0);
  setpx(src.data(), 8, 0, 0, a);
  setpx(src.data(), 8, 1, 0, b);
  setpx(src.data(), 8, 0, 1, c);
  setpx(src.data(), 8, 1, 1, d);
  const auto turned = lundukepaint::rotate_nearest_angle(src.data(), 2, 2, 8, nullptr, 90);
  int seen = 0;
  for (int y = 0; y < turned.height; ++y) {
    for (int x = 0; x < turned.width; ++x) {
      const Color px = get(turned.rgba.data(), turned.width * 4, x, y);
      if (px == a || px == b || px == c || px == d) {
        ++seen;
      }
    }
  }
  expect(seen == 4, "a 90 degree rotate keeps every source pixel");

  std::vector<std::uint8_t> row(3 * 2 * 4, 0);
  setpx(row.data(), 12, 0, 0, a);
  setpx(row.data(), 12, 1, 0, b);
  setpx(row.data(), 12, 0, 1, c);
  setpx(row.data(), 12, 1, 1, d);
  const auto sheared = lundukepaint::shear_horizontal_nearest(row.data(), 3, 2, 12, nullptr, 2);
  expect(sheared.width == 5, "shear grows by the shift");
  expect(get(sheared.rgba.data(), sheared.width * 4, 0, 0) == a, "the top row stays put");
  expect(get(sheared.rgba.data(), sheared.width * 4, 2, 1) == c, "the bottom row shifts right");
}

void test_gif() {
  const int w = 4;
  const int h = 2;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4), 255);
  setpx(rgba.data(), w * 4, 0, 0, {255, 0, 0, 255});
  setpx(rgba.data(), w * 4, 1, 0, {0, 255, 0, 255});
  setpx(rgba.data(), w * 4, 2, 0, {0, 0, 255, 255});
  setpx(rgba.data(), w * 4, 3, 0, {0, 0, 0, 0});
  std::vector<std::uint8_t> encoded;
  std::string error;
  expect(lundukepaint::encode_gif_memory(rgba.data(), w, h, w * 4, encoded, error), "encode gif");
  expect(encoded.size() > 16 && encoded[0] == 'G' && encoded[1] == 'I' && encoded[2] == 'F', "gif magic");
  bool gce = false;
  for (std::size_t i = 0; i + 2 < encoded.size(); ++i) {
    if (encoded[i] == 0x21 && encoded[i + 1] == 0xf9) {
      gce = true;
    }
  }
  expect(gce, "a clear pixel adds a transparent index");
  const std::string path = std::string("/tmp/lunduke-round2-") + std::to_string(getpid()) + ".gif";
  expect(lundukepaint::save_flat_image(path, lundukepaint::ImageFormat::Gif, rgba.data(), w, h, w * 4, 90,
                                      error),
         "save gif");
  const auto loaded = lundukepaint::load_flat_image(path);
  expect(loaded.ok() && loaded.width == w && loaded.height == h, "reload gif size");
  if (loaded.ok()) {
    expect(get(loaded.rgba.data(), w * 4, 0, 0) == (Color{255, 0, 0, 255}), "red survives");
    expect(get(loaded.rgba.data(), w * 4, 1, 0) == (Color{0, 255, 0, 255}), "green survives");
    expect(get(loaded.rgba.data(), w * 4, 2, 0) == (Color{0, 0, 255, 255}), "blue survives");
    expect(get(loaded.rgba.data(), w * 4, 3, 0).a == 0, "clear pixel stays clear");
  }
  unlink(path.c_str());

  std::vector<std::uint8_t> ramp(32 * 8 * 4, 255);
  for (int i = 0; i < 32 * 8; ++i) {
    ramp[static_cast<std::size_t>(i) * 4] = static_cast<std::uint8_t>(i * 3);
    ramp[static_cast<std::size_t>(i) * 4 + 1] = static_cast<std::uint8_t>(255 - (i % 64));
    ramp[static_cast<std::size_t>(i) * 4 + 2] = static_cast<std::uint8_t>(i * 5);
  }
  expect(lundukepaint::encode_gif_memory(ramp.data(), 32, 8, 128, encoded, error), "reduce gif");
  const std::string ramp_path = std::string("/tmp/lunduke-round2-ramp-") + std::to_string(getpid()) + ".gif";
  expect(lundukepaint::save_flat_image(ramp_path, lundukepaint::ImageFormat::Gif, ramp.data(), 32, 8, 128, 90,
                                      error),
         "save reduced gif");
  const auto reduced = lundukepaint::load_flat_image(ramp_path);
  expect(reduced.ok(), "reload reduced gif");
  if (reduced.ok()) {
    std::map<std::uint32_t, int> colors;
    for (int i = 0; i < reduced.width * reduced.height; ++i) {
      const Color c = get(reduced.rgba.data(), reduced.width * 4, i % reduced.width, i / reduced.width);
      const std::uint32_t key = (static_cast<std::uint32_t>(c.r) << 16) | (static_cast<std::uint32_t>(c.g) << 8) | c.b;
      colors[key] = 1;
    }
    expect(static_cast<int>(colors.size()) <= 256, "gif holds at most 256 colors");
  }
  unlink(ramp_path.c_str());
}

void test_brush_and_arrow() {
  std::vector<std::uint8_t> px(8, 0);
  setpx(px.data(), 8, 0, 0, {255, 0, 0, 255});
  setpx(px.data(), 8, 1, 0, {0, 0, 0, 0});
  CustomBrush brush = lundukepaint::custom_brush_from_rgba(px.data(), 2, 1, 8, Color::white());
  expect(brush.width == 2 && brush.rgba[3] == 255 && brush.rgba[7] == 0, "pickup keeps ink");
  lundukepaint::custom_brush_flip_h(brush);
  expect(brush.rgba[3] == 0 && brush.rgba[4] == 255, "flip h swaps the columns");
  lundukepaint::custom_brush_rotate_90(brush);
  expect(brush.width == 1 && brush.height == 2, "rotate 90 swaps the sides");
  lundukepaint::custom_brush_remap(brush, {0, 0, 255, 255});
  bool blue = false;
  for (int i = 0; i < brush.width * brush.height; ++i) {
    if (brush.rgba[static_cast<std::size_t>(i) * 4 + 3] != 0) {
      blue = brush.rgba[static_cast<std::size_t>(i) * 4 + 2] == 255;
    }
  }
  expect(blue, "remap paints opaque pixels with the foreground");
  brush.spacing = 5;
  lundukepaint::custom_brush_scale(brush, 2, 1);
  expect(brush.spacing == 5, "scale keeps the spacing");
  expect(brush.width == 2 && brush.height == 4, "double nearest");

  std::vector<std::uint8_t> canvas;
  fill(canvas, 24, 16, Color::white());
  lundukepaint::draw_line_arrows(canvas.data(), 24, 16, 96, 2, 8, 18, 8, 2, Color::black(), false, true,
                                 nullptr);
  int off = 0;
  for (int y = 5; y <= 11; ++y) {
    for (int x = 12; x <= 18; ++x) {
      if (y != 8 && get(canvas.data(), 96, x, y) == Color::black()) {
        ++off;
      }
    }
  }
  expect(off > 0, "the arrowhead is wider than the line");

  fill(canvas, 12, 6, Color::white());
  setpx(canvas.data(), 48, 2, 2, {255, 0, 0, 255});
  const lundukepaint::BrushTip tip = lundukepaint::brush_tip_at(3);
  lundukepaint::smear_brush_tip(canvas.data(), 12, 6, 48, 2, 2, 8, 2, tip, nullptr);
  int dragged = 0;
  for (int x = 3; x <= 8; ++x) {
    if (get(canvas.data(), 48, x, 2).r > 200) {
      ++dragged;
    }
  }
  expect(dragged > 0, "smear drags the red pixel along the stroke");
  expect(lundukepaint::step_choice(1, lundukepaint::kLineWidthChoices, lundukepaint::kLineWidthChoiceCount, 1) == 2,
         "[ ] steps to the next line width");
  expect(lundukepaint::step_choice(8, lundukepaint::kLineWidthChoices, lundukepaint::kLineWidthChoiceCount, 1) == 8,
         "the last width stays put");
  expect(lundukepaint::step_choice(16, lundukepaint::kSprayRadiusChoices, lundukepaint::kSprayRadiusChoiceCount,
                                   -1) == 12,
         "spray radius steps down a row");
}

void test_swatch() {
  expect(lundukepaint::ColorsPanel::swatch_index(Color{0, 0, 0, 255}) >= 0, "black is a swatch");
  expect(lundukepaint::ColorsPanel::swatch_index(Color{1, 2, 3, 255}) < 0, "an odd color has no index");
  expect(lundukepaint::ColorsPanel::swatch_index(Color{0, 0, 0, 0}) < 0, "clear is not a swatch");
}

}  // namespace

int main() {
  test_halftone();
  test_diffuse();
  test_shadow_wind_crt();
  test_selection_morph();
  test_rotate_shear();
  test_gif();
  test_brush_and_arrow();
  test_swatch();
  if (errors != 0) {
    std::fprintf(stderr, "test_round2: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_round2: ok\n");
  return 0;
}
