// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "raster/effects.hpp"

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

  if (errors != 0) {
    std::fprintf(stderr, "test_effects: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_effects: ok\n");
  return 0;
}
