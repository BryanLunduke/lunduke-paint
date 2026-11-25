// SPDX-License-Identifier: GPL-3.0-or-later
// Regression coverage for the 1.1.0 review: GIF LZW, smear direction, wind,
// halftone alpha, shear of a one-row selection, free-rotate undo, repeat
// selection, and brush size keys.

#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/effect_preview.hpp"
#include "doc/history.hpp"
#include "doc/selection.hpp"
#include "io/image_io.hpp"
#include "raster/brush_tip.hpp"
#include "raster/effects.hpp"
#include "raster/transform.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using lundukepaint::BrushTip;
using lundukepaint::BrushTipKind;
using lundukepaint::BrushTipStep;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::EffectPreview;
using lundukepaint::HalftoneScreen;
using lundukepaint::Layer;
using lundukepaint::ImageFormat;
using lundukepaint::Rect;
using lundukepaint::WindDirection;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_review_r16: %s\n", msg);
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

void test_smear_directions() {
  const Color red{220, 30, 30, 255};
  const Color white = Color::white();
  const BrushTip tip{BrushTipKind::Square, 5};
  auto line = [&](double x0, double y0, double x1, double y1, int expect_x, int expect_y, const char* msg) {
    std::vector<std::uint8_t> buf(40 * 24 * 4, 255);
    for (int y = 6; y < 18; ++y) {
      setpx(buf.data(), 160, 16, y, red);
    }
    for (int x = 10; x < 22; ++x) {
      setpx(buf.data(), 160, x, 4, red);
    }
    Rect dirty{};
    lundukepaint::smear_brush_tip(buf.data(), 40, 24, 160, x0, y0, x1, y1, tip, &dirty);
    expect(get(buf.data(), 160, expect_x, expect_y) == red, msg);
  };
  // A thin mark dragged with the motion must survive in every direction.
  // Right/down used to erase it or leave a one-pixel stripe.
  line(16, 12, 30, 12, 30, 12, "smear right keeps the mark");
  line(16, 12, 4, 12, 4, 12, "smear left keeps the mark");
  line(16, 4, 16, 18, 16, 18, "smear down keeps the mark");
  line(16, 12, 16, 2, 16, 3, "smear up keeps the mark");

  std::vector<std::uint8_t> block(40 * 24 * 4, 255);
  for (int y = 8; y < 16; ++y) {
    for (int x = 8; x < 16; ++x) {
      setpx(block.data(), 160, x, y, red);
    }
  }
  Rect dirty{};
  lundukepaint::smear_brush_tip(block.data(), 40, 24, 160, 12, 12, 30, 12, tip, &dirty);
  int gaps = 0;
  bool seen = false;
  for (int x = 8; x < 30; ++x) {
    const bool on = get(block.data(), 160, x, 12) == red;
    if (on) {
      seen = true;
    } else if (seen) {
      ++gaps;
    }
  }
  expect(seen && gaps == 0, "smear right is a solid streak");
  expect(get(block.data(), 160, 0, 0) == white, "smear leaves the far corner alone");
}

void test_wind_solid() {
  const int w = 24;
  const int h = 10;
  std::vector<std::uint8_t> buf(static_cast<std::size_t>(w * h * 4), 255);
  for (int y = 3; y < 7; ++y) {
    for (int x = 6; x < 14; ++x) {
      setpx(buf.data(), w * 4, x, y, Color::black());
    }
  }
  lundukepaint::wind_rgba(buf.data(), w, h, w * 4, WindDirection::Right, 8, 3, Color::white());
  expect(get(buf.data(), w * 4, 0, 3) == Color::white(), "the left border does not streak");
  expect(get(buf.data(), w * 4, 6, 4) == Color::black(), "the windward edge stays");
  expect(get(buf.data(), w * 4, 10, 4) == Color::black(), "the solid interior stays");
  int outside = 0;
  for (int y = 3; y < 7; ++y) {
    for (int x = 14; x < w; ++x) {
      if (get(buf.data(), w * 4, x, y) == Color::black()) {
        ++outside;
      }
    }
  }
  expect(outside >= 4, "wind leaves the shape toward the right");
  for (int x = 0; x < 6; ++x) {
    expect(get(buf.data(), w * 4, x, 4) == Color::white(), "wind does not run back into the shape");
  }

  std::fill(buf.begin(), buf.end(), 255);
  for (int y = 2; y < 8; ++y) {
    for (int x = 8; x < 16; ++x) {
      setpx(buf.data(), w * 4, x, y, Color::black());
    }
  }
  lundukepaint::wind_rgba(buf.data(), w, h, w * 4, WindDirection::Left, 6, 9, Color::white());
  int left = 0;
  for (int x = 0; x < 8; ++x) {
    if (get(buf.data(), w * 4, x, 4) == Color::black()) {
      ++left;
    }
  }
  expect(left >= 1, "wind left leaves the shape");
  expect(get(buf.data(), w * 4, w - 1, 0) == Color::white(), "the right border does not streak");
}

void test_halftone_alpha() {
  std::vector<std::uint8_t> buf(8 * 8 * 4, 0);
  setpx(buf.data(), 32, 1, 1, Color{200, 200, 200, 255});
  setpx(buf.data(), 32, 3, 3, Color{0, 0, 0, 0});
  lundukepaint::halftone_rgba(buf.data(), 8, 8, 32, Color::black(), Color::white(), 1,
                             HalftoneScreen::Bayer);
  expect(get(buf.data(), 32, 3, 3).a == 0, "halftone keeps a clear pixel clear");
  expect(get(buf.data(), 32, 0, 0).a == 0, "halftone does not paint the empty margin black");
  const Color painted = get(buf.data(), 32, 1, 1);
  expect(painted == Color::black() || painted == Color::white(), "an opaque pixel is still ink or paper");
}

void test_shear_degenerate() {
  std::vector<std::uint8_t> row(4 * 4, 0);
  setpx(row.data(), 16, 1, 0, Color{9, 8, 7, 255});
  const auto sheared = lundukepaint::shear_horizontal_nearest(row.data(), 4, 1, 16, nullptr, 5);
  expect(sheared.width == 4 && sheared.height == 1, "a one-row shear does not add empty space");
  expect(get(sheared.rgba.data(), sheared.width * 4, 1, 0) == (Color{9, 8, 7, 255}),
         "the one-row pixels stay put");

  std::vector<std::uint8_t> col(4 * 3, 0);
  setpx(col.data(), 4, 0, 1, Color{1, 2, 3, 255});
  const auto down = lundukepaint::shear_vertical_nearest(col.data(), 1, 3, 4, nullptr, 4);
  expect(down.width == 1 && down.height == 3, "a one-column shear does not add empty space");
  expect(get(down.rgba.data(), 4, 0, 1) == (Color{1, 2, 3, 255}), "the one-column pixels stay put");
}

void test_repeat_selection() {
  auto doc = Document::create(8, 8, Color::white());
  Layer& layer = doc->layers().active_layer();
  doc->selection().set_rect({0, 0, 2, 2});
  EffectPreview first(*doc);
  auto invert = [](std::uint8_t* px, int w, int h, int stride) {
    lundukepaint::invert_rgba(px, w, h, stride);
  };
  expect(first.commit("Invert", invert), "invert commits");
  const Color kept = layer.pixel(0, 0);
  const Color outside = layer.pixel(7, 7);
  expect(kept != Color::white(), "the first selection is inverted");
  expect(outside == Color::white(), "pixels outside the selection stay");
  doc->selection().set_rect({4, 4, 2, 2});
  EffectPreview second(*doc);
  expect(second.commit("Invert", invert), "repeat commits on the new selection");
  expect(layer.pixel(0, 0) == kept, "repeat does not reuse the old selection");
  expect(layer.pixel(4, 4) != Color::white(), "repeat hits the current selection");
  expect(layer.pixel(7, 7) == outside, "repeat leaves the unselected corner");
}

void test_free_rotate_undo() {
  auto doc = Document::create(16, 16, Color::white());
  Layer& layer = doc->layers().active_layer();
  layer.set_pixel(4, 4, Color{10, 20, 30, 255});
  layer.set_pixel(6, 5, Color{40, 50, 60, 255});
  doc->selection().set_rect({4, 4, 4, 4});
  const auto before = doc->selection().capture();
  expect(doc->selection().lift(layer, 0), "lift");
  const std::uint8_t* coverage = doc->selection().float_coverage();
  auto turned = lundukepaint::rotate_nearest_angle(doc->selection().float_pixels(), doc->selection().float_w(),
                                                  doc->selection().float_h(), doc->selection().float_w() * 4,
                                                  coverage, 90);
  const int nx = doc->selection().float_x();
  const int ny = doc->selection().float_y();
  doc->selection().transform_float(nx, ny, turned.width, turned.height, std::move(turned.rgba),
                                   std::move(turned.coverage));
  doc->arm_pending_transform("Free rotate", before);
  expect(!doc->history().can_undo(), "free rotate is not a history step while floating");
  expect(doc->cancel_floating(), "escape cancels the float");
  expect(!doc->selection().floating(), "escape drops the float");
  expect(!doc->history().can_undo(), "escape writes no history entry");
  expect(layer.pixel(4, 4) == (Color{10, 20, 30, 255}), "escape leaves the layer pixels");
  expect(doc->selection().bounds().x == 4 && doc->selection().bounds().w == 4, "escape restores the selection");

  expect(doc->selection().lift(layer, 0), "lift again");
  coverage = doc->selection().float_coverage();
  turned = lundukepaint::rotate_nearest_angle(doc->selection().float_pixels(), doc->selection().float_w(),
                                             doc->selection().float_h(), doc->selection().float_w() * 4, coverage,
                                             90);
  doc->selection().transform_float(nx, ny, turned.width, turned.height, std::move(turned.rgba),
                                   std::move(turned.coverage));
  doc->arm_pending_transform("Free rotate", before);
  layer.set_locked(true);
  expect(!doc->try_commit_floating("Move selection"), "a locked layer cannot place the rotate");
  expect(doc->pending_transform() && doc->selection().floating(), "the float stays up when place is refused");
  layer.set_locked(false);
  expect(doc->try_commit_floating("Move selection"), "place stamps the rotate");
  expect(!doc->selection().floating(), "place drops the float");
  expect(doc->history().can_undo(), "place is one undo step");
  expect(doc->history().name_at(doc->history().index()) == "Free rotate", "the step is named Free rotate");
  doc->undo();
  expect(!doc->selection().floating(), "undo does not return to the float");
  expect(layer.pixel(4, 4) == (Color{10, 20, 30, 255}), "undo restores the pixels");
  expect(doc->selection().bounds().w == 4, "undo restores the selection");
  expect(!doc->history().can_undo(), "one undo consumes the rotate");
}

void test_brush_size_keys() {
  const BrushTipStep up = lundukepaint::step_brush_tip_size(0, 0, 1);
  expect(up.index == 1 && up.size == 2, "[ ] grows the round tip instead of changing shape");
  const BrushTipStep across = lundukepaint::step_brush_tip_size(3, 8, 1);
  expect(lundukepaint::brush_tip_at(across.index).kind == BrushTipKind::Round, "the shape stays round");
  expect(across.size > 8, "the largest round tip still grows");
  const BrushTipStep down = lundukepaint::step_brush_tip_size(3, 8, -1);
  expect(down.size == 4 && lundukepaint::brush_tip_at(down.index).kind == BrushTipKind::Round,
         "] then [ walks round sizes");
}

bool pillow_loads(const std::string& path, int expect_w, int expect_h) {
  const std::string script =
      "from PIL import Image\n"
      "im = Image.open(" + std::string("'") + path + "')\n"
      "im.load()\n"
      "rgba = im.convert('RGBA')\n"
      "assert rgba.size == (" + std::to_string(expect_w) + ", " + std::to_string(expect_h) + ")\n"
      "rgba.save('" + path + ".png')\n";
  const std::string cmd = "python3 -c " + std::string("'") + script + "'";
  // The script has quotes. Write it to a file instead.
  const std::string py = path + ".py";
  FILE* out = std::fopen(py.c_str(), "w");
  if (out == nullptr) {
    return false;
  }
  std::fputs(script.c_str(), out);
  std::fclose(out);
  const std::string run = "python3 " + py;
  const int rc = std::system(run.c_str());
  std::remove(py.c_str());
  return rc == 0;
}

void test_gif_roundtrip() {
  const int w = 32;
  const int h = 24;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4), 255);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      Color c{static_cast<std::uint8_t>((x * 7) % 200), static_cast<std::uint8_t>((y * 11) % 180),
              static_cast<std::uint8_t>((x + y) % 90), 255};
      if (x < 2 && y < 2) {
        c = Color::transparent();
      }
      setpx(rgba.data(), w * 4, x, y, c);
    }
  }
  std::vector<std::uint8_t> encoded;
  std::string error;
  expect(lundukepaint::encode_gif_memory(rgba.data(), w, h, w * 4, encoded, error), "encode varied gif");
  const std::string path = std::string("/tmp/lunduke-r16-") + std::to_string(getpid()) + ".gif";
  FILE* f = std::fopen(path.c_str(), "wb");
  expect(f != nullptr, "open gif");
  if (f != nullptr) {
    expect(std::fwrite(encoded.data(), 1, encoded.size(), f) == encoded.size(), "write gif");
    std::fclose(f);
  }
  expect(pillow_loads(path, w, h), "Pillow decodes the GIF");
  lundukepaint::LoadedImage back = lundukepaint::load_flat_image(path + ".png");
  expect(back.ok(), "reload the Pillow PNG");
  if (back.ok()) {
    const Color clear = get(back.rgba.data(), back.width * 4, 0, 0);
    expect(clear.a == 0, "transparency survives an independent decode");
    const Color kept = get(rgba.data(), w * 4, 8, 8);
    const Color got = get(back.rgba.data(), back.width * 4, 8, 8);
    const int dr = std::abs(static_cast<int>(kept.r) - got.r);
    const int dg = std::abs(static_cast<int>(kept.g) - got.g);
    const int db = std::abs(static_cast<int>(kept.b) - got.b);
    expect(dr < 24 && dg < 24 && db < 24, "a reduced color stays near the source");
  }
  std::remove(path.c_str());
  std::remove((path + ".png").c_str());
}

}  // namespace

int main() {
  test_smear_directions();
  test_wind_solid();
  test_halftone_alpha();
  test_shear_degenerate();
  test_repeat_selection();
  test_free_rotate_undo();
  test_brush_size_keys();
  test_gif_roundtrip();
  if (errors != 0) {
    std::fprintf(stderr, "test_review_r16: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_review_r16: ok\n");
  return 0;
}
