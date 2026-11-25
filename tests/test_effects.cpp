// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "raster/custom_brush.hpp"
#include "raster/effects.hpp"
#include "raster/palette.hpp"
#include "raster/pattern.hpp"
#include "raster/grid_snap.hpp"

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
    std::fprintf(stderr, "test_effects: %s\n", msg);
    return 1;
  }
  return 0;
}

Color get(const std::uint8_t* buf, int stride, int x, int y) {
  const std::uint8_t* p = buf + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                          static_cast<std::size_t>(x) * 4;
  return {p[0], p[1], p[2], p[3]};
}

void set(std::uint8_t* buf, int stride, int x, int y, Color c) {
  std::uint8_t* p = buf + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride) +
                    static_cast<std::size_t>(x) * 4;
  p[0] = c.r;
  p[1] = c.g;
  p[2] = c.b;
  p[3] = c.a;
}

}  // namespace

int main() {
  int errors = 0;

  // Invert a 2×2 buffer; alpha stays.
  {
    const int w = 2;
    const int h = 2;
    const int stride = w * 4;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(stride * h), 0);
    set(buf.data(), stride, 0, 0, Color{0, 0, 0, 255});
    set(buf.data(), stride, 1, 0, Color{255, 128, 0, 200});
    set(buf.data(), stride, 0, 1, Color{10, 20, 30, 0});
    set(buf.data(), stride, 1, 1, Color{255, 255, 255, 255});
    lundukepaint::invert_rgba(buf.data(), w, h, stride);
    errors += expect(get(buf.data(), stride, 0, 0) == (Color{255, 255, 255, 255}),
                     "invert black -> white");
    errors += expect(get(buf.data(), stride, 1, 0) == (Color{0, 127, 255, 200}),
                     "invert orange keeps alpha");
    errors += expect(get(buf.data(), stride, 0, 1) == (Color{245, 235, 225, 0}),
                     "invert dark keeps zero alpha");
    errors += expect(get(buf.data(), stride, 1, 1) == (Color{0, 0, 0, 255}), "invert white -> black");
  }

  // Grayscale is a luma mix.
  {
    const int w = 1;
    const int h = 1;
    std::vector<std::uint8_t> buf{255, 0, 0, 255};
    lundukepaint::grayscale_rgba(buf.data(), w, h, 4);
    errors += expect(buf[0] == buf[1] && buf[1] == buf[2], "grayscale equal channels");
    errors += expect(buf[0] > 0 && buf[0] < 255, "red luma is mid-dark");
    errors += expect(buf[3] == 255, "grayscale keeps alpha");
  }

  // Invert as an undoable command on a tiny document.
  {
    auto doc = Document::create(4, 3, Color{40, 50, 60, 255}, "Background");
    Layer& layer = doc->layers().active_layer();
    Layer before(layer.width(), layer.height(), Color::transparent(), "before");
    before.copy_from(layer);
    lundukepaint::invert_rgba(layer.pixels(), layer.width(), layer.height(), layer.stride());
    Layer after(layer.width(), layer.height(), Color::transparent(), "after");
    after.copy_from(layer);
    auto cmd = PixelPatchCommand::from_layers(before, after, Rect{0, 0, 4, 3}, "Invert");
    errors += expect(!cmd->empty(), "invert produced a patch");
    doc->history().commit_applied(std::move(cmd));
    errors += expect(layer.pixel(0, 0) == (Color{215, 205, 195, 255}), "invert applied");
    doc->history().undo(*doc);
    errors += expect(layer.pixel(0, 0) == (Color{40, 50, 60, 255}), "undo invert");
    doc->history().redo(*doc);
    errors += expect(layer.pixel(0, 0) == (Color{215, 205, 195, 255}), "redo invert");
  }

  // Box blur on a 3×3 white-on-black plus undo invertibility.
  {
    const int w = 3;
    const int h = 3;
    const int stride = w * 4;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(stride * h), 0);
    std::vector<std::uint8_t> dest(src.size(), 0);
    for (int i = 3; i < static_cast<int>(src.size()); i += 4) {
      src[static_cast<std::size_t>(i)] = 255;
    }
    set(src.data(), stride, 1, 1, Color{255, 255, 255, 255});
    lundukepaint::box_blur_rgba(src.data(), w, h, stride, dest.data(), stride, 1);
    const Color center = get(dest.data(), stride, 1, 1);
    errors += expect(center.r < 255 && center.r > 0, "blur softens white center");
    errors += expect(center.a == 255, "blur keeps opaque alpha at center");
    const Color corner = get(dest.data(), stride, 0, 0);
    errors += expect(corner.r > 0 && corner.r < 255, "blur leaks into black corner");

    auto doc = Document::create(3, 3, Color::black(), "Background");
    Layer& layer = doc->layers().active_layer();
    layer.set_pixel(1, 1, Color::white());
    Layer before(layer.width(), layer.height(), Color::transparent(), "before");
    before.copy_from(layer);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(layer.stride()) *
                                 static_cast<std::size_t>(layer.height()));
    lundukepaint::box_blur_rgba(layer.pixels(), layer.width(), layer.height(), layer.stride(),
                            out.data(), layer.stride(), 1);
    layer.write_rect(Rect{0, 0, 3, 3}, out.data());
    Layer after(layer.width(), layer.height(), Color::transparent(), "after");
    after.copy_from(layer);
    auto cmd = PixelPatchCommand::from_layers(before, after, Rect{0, 0, 3, 3}, "Blur");
    errors += expect(!cmd->empty(), "blur produced a patch");
    const Color blurred = layer.pixel(1, 1);
    doc->history().commit_applied(std::move(cmd));
    doc->history().undo(*doc);
    errors += expect(layer.pixel(1, 1) == Color::white(), "undo blur");
    doc->history().redo(*doc);
    errors += expect(layer.pixel(1, 1) == blurred, "redo blur");
  }

  // Premultiplied blur must not darken opaque color beside transparency.
  {
    const int w = 2;
    const int h = 1;
    const int stride = w * 4;
    std::vector<std::uint8_t> src(static_cast<std::size_t>(stride * h), 0);
    std::vector<std::uint8_t> dest(src.size(), 0);
    set(src.data(), stride, 0, 0, Color{255, 0, 0, 255});
    lundukepaint::box_blur_rgba(src.data(), w, h, stride, dest.data(), stride, 1);
    const Color edge = get(dest.data(), stride, 0, 0);
    errors += expect(edge.r == 255, "blur keeps red next to transparency");
    errors += expect(edge.a > 0 && edge.a < 255, "blur softens alpha at the fringe");
    const Color clear = get(dest.data(), stride, 1, 0);
    errors += expect(clear.r == 255 || clear.a == 0, "transparent neighbor is not blackened");
  }

  {
    constexpr int kW = 12;
    constexpr int kH = 12;
    const int stride = kW * 4;
    std::vector<std::uint8_t> buf(static_cast<std::size_t>(stride) * kH);
    for (int i = 0; i < kW * kH; ++i) {
      set(buf.data(), stride, i % kW, i / kW, Color::white());
    }
    for (int y = 3; y <= 8; ++y) {
      for (int x = 3; x <= 8; ++x) {
        set(buf.data(), stride, x, y, Color::black());
      }
    }
    lundukepaint::trace_edges_rgba(buf.data(), kW, kH, stride, Color::black(), Color::white());
    errors += expect(get(buf.data(), stride, 3, 3) == Color::black(), "trace keeps the corner");
    errors += expect(get(buf.data(), stride, 5, 3) == Color::black(), "trace keeps the top edge");
    errors += expect(get(buf.data(), stride, 5, 5) == Color::white(), "trace clears the interior");
    errors += expect(get(buf.data(), stride, 1, 1) == Color::white(), "trace leaves the paper");
  }

  {
    const double zooms[] = {0.25, 0.5, 1.0, 2.0, 3.0, 8.0};
    const double origin = 24.0;
    bool stable = true;
    for (double zoom : zooms) {
      const double canvas = 20.2;
      const double widget = origin + canvas * zoom;
      const double recovered = lundukepaint::canvas_from_widget(widget, origin, zoom);
      if (lundukepaint::snap_canvas_coord(recovered, 8) != lundukepaint::snap_canvas_coord(canvas, 8)) {
        stable = false;
      }
    }
    errors += expect(stable, "8px grid snap is the same at every zoom");
    errors += expect(lundukepaint::snap_canvas_coord(0.2, 8) == 0, "near zero snaps to 0");
    errors += expect(lundukepaint::snap_canvas_coord(5.0, 8) == 8, "mid cell snaps to the next grid line");
    errors += expect(lundukepaint::snap_canvas_coord(3.0, 8) == 0, "early cell snaps back");
  }

  {
    std::uint8_t src[16] = {255, 255, 255, 255, 200, 0, 0, 255, 255, 255, 255, 255, 0, 0, 0, 0};
    const auto brush = lundukepaint::custom_brush_from_rgba(src, 2, 2, 8, Color::white());
    errors += expect(brush.width == 2 && brush.height == 2, "brush keeps a small selection");
    errors += expect(brush.rgba[3] == 0, "background color is punched out");
    errors += expect(brush.rgba[7] == 255 && brush.rgba[4] == 200, "foreground pixel is kept");
    errors += expect(brush.rgba[15] == 0, "clear pixel stays clear");
    std::vector<std::uint8_t> canvas(8 * 8 * 4, 0);
    for (int i = 0; i < 64; ++i) {
      canvas[i * 4 + 0] = 1;
      canvas[i * 4 + 3] = 255;
    }
    lundukepaint::stamp_custom_brush(canvas.data(), 8, 8, 32, 3, 3, brush, nullptr);
    const Color stamped = get(canvas.data(), 32, 3, 2);
    errors += expect(stamped.r == 200 && stamped.a == 255, "brush stamps the opaque pixel");
    const Color skipped = get(canvas.data(), 32, 2, 2);
    errors += expect(skipped.r == 1, "transparent brush pixel does not cover the canvas");
  }

  {
    std::uint8_t src[8 * 4];
    for (int x = 0; x < 8; ++x) {
      src[x * 4 + 0] = x < 4 ? 0 : 255;
      src[x * 4 + 1] = x < 4 ? 0 : 255;
      src[x * 4 + 2] = x < 4 ? 0 : 255;
      src[x * 4 + 3] = 255;
    }
    const lundukepaint::Pattern made =
        lundukepaint::pattern_from_rgba(src, 8, 1, 32, Color::white());
    errors += expect(made.bit_at(0, 0) && made.bit_at(3, 0), "dark samples set pattern bits");
    errors += expect(!made.bit_at(4, 0) && !made.bit_at(7, 0), "paper samples stay clear");
    const int before = lundukepaint::pattern_count();
    const int index = lundukepaint::add_custom_pattern(made);
    errors += expect(index == before, "custom pattern is appended");
    errors += expect(lundukepaint::pattern_at(index).rows[0] == made.rows[0], "lookup finds the new pattern");
    lundukepaint::set_custom_patterns({});
  }

  {
    const lundukepaint::RetroPalette& mac = lundukepaint::retro_palette_at(0);
    errors += expect(mac.width == 512 && mac.height == 342 && mac.count == 2, "Mac preset is 512x342 1-bit");
    const lundukepaint::RetroPalette& gb = lundukepaint::retro_palette_at(5);
    errors += expect(gb.width == 160 && gb.height == 144 && gb.count == 4, "Game Boy preset is 160x144");
    std::vector<std::uint8_t> row(16 * 4);
    for (int x = 0; x < 16; ++x) {
      set(row.data(), 64, x, 0, Color{128, 128, 128, 255});
    }
    lundukepaint::quantize_palette(row.data(), 16, 1, 64, mac.colors, mac.count,
                                   lundukepaint::DitherKind::Ordered);
    int black = 0;
    int white = 0;
    bool only = true;
    for (int x = 0; x < 16; ++x) {
      const Color c = get(row.data(), 64, x, 0);
      if (c == Color::black()) {
        ++black;
      } else if (c == Color::white()) {
        ++white;
      } else {
        only = false;
      }
    }
    errors += expect(only && black > 0 && white > 0, "ordered 1-bit dither mixes black and white");

    std::vector<std::uint8_t> ramp(8 * 4);
    for (int x = 0; x < 8; ++x) {
      set(ramp.data(), 32, x, 0, Color{static_cast<std::uint8_t>(x * 32), static_cast<std::uint8_t>(x * 32),
                                       static_cast<std::uint8_t>(x * 32), 200});
    }
    lundukepaint::quantize_palette(ramp.data(), 8, 1, 32, mac.colors, mac.count,
                                   lundukepaint::DitherKind::FloydSteinberg);
    bool in_palette = true;
    bool alpha_kept = true;
    for (int x = 0; x < 8; ++x) {
      const Color c = get(ramp.data(), 32, x, 0);
      const bool mono = (c.r == 0 && c.g == 0 && c.b == 0) || (c.r == 255 && c.g == 255 && c.b == 255);
      if (!mono) {
        in_palette = false;
      }
      if (c.a != 200) {
        alpha_kept = false;
      }
    }
    errors += expect(in_palette, "Floyd–Steinberg lands on palette colors");
    errors += expect(alpha_kept, "quantize keeps alpha");
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_effects: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_effects: ok\n");
  return 0;
}
