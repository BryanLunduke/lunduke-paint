// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless coverage for the on-canvas text box: word wrap is layout (the
// string is not rewritten), resizing reflows, a stamp is one undo step, and
// a click outside the box is the commit action. Pointer chrome, the caret
// timer, and the file-chooser preview widget are not driven here.

#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"
#include "raster/text_box.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::ImageFormat;
using lundukepaint::Layer;
using lundukepaint::LoadedImage;
using lundukepaint::Rect;
using lundukepaint::TextBoxHit;
using lundukepaint::TextBoxMetrics;
using lundukepaint::TextBoxPress;
using lundukepaint::TextBoxState;
using lundukepaint::commit_text_box;
using lundukepaint::hit_test_text_box;
using lundukepaint::layout_text_box;
using lundukepaint::load_image_preview;
using lundukepaint::move_text_box;
using lundukepaint::render_text_box;
using lundukepaint::resize_text_box;
using lundukepaint::save_flat_image;
using lundukepaint::save_ora;
using lundukepaint::text_box_backspace;
using lundukepaint::text_box_border_slop;
using lundukepaint::text_box_handle_radius;
using lundukepaint::text_box_insert;
using lundukepaint::text_box_line_count;
using lundukepaint::text_box_move_left;
using lundukepaint::text_box_press_action;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_text_box: %s\n", msg);
    ++errors;
  }
}

std::vector<std::uint8_t> dump(const Layer& layer) {
  const std::size_t n = static_cast<std::size_t>(layer.stride()) * static_cast<std::size_t>(layer.height());
  return std::vector<std::uint8_t>(layer.pixels(), layer.pixels() + n);
}

TextBoxState sample_box(int width) {
  TextBoxState state;
  state.family = "Sans";
  state.size_pt = 18;
  state.box = Rect{8, 8, width, 220};
  state.text = "alpha beta gamma delta epsilon zeta eta theta";
  state.color = Color::black();
  return state;
}

std::string temp_path(const char* suffix) {
  char path[] = "/tmp/lunduke-text-XXXXXX";
  const int fd = mkstemp(path);
  if (fd >= 0) {
    close(fd);
    unlink(path);
  }
  return std::string(path) + suffix;
}

bool write_bytes(const std::string& path, const void* data, std::size_t size) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    return false;
  }
  out.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
  return static_cast<bool>(out);
}

}  // namespace

int main() {
  // Word wrap follows the box width and does not insert line breaks.
  {
    TextBoxState wide = sample_box(1600);
    TextBoxState narrow = sample_box(36);
    const std::string original = wide.text;
    TextBoxMetrics wide_m;
    TextBoxMetrics narrow_m;
    layout_text_box(wide, wide_m);
    layout_text_box(narrow, narrow_m);
    expect(wide.text == original, "wide layout left the string alone");
    expect(narrow.text == original, "narrow layout left the string alone");
    expect(original.find('\n') == std::string::npos, "sample has no hard breaks");
    expect(wide_m.line_count >= 1, "wide layout has a line");
    expect(narrow_m.line_count > wide_m.line_count, "narrow box wraps onto more lines");
    expect(narrow_m.layout_height > wide_m.layout_height, "reflow grows the text block");
    expect(text_box_line_count(narrow) == narrow_m.line_count, "line_count matches layout");
  }

  // A hard break the user pasted stays, and the long line still reflows.
  {
    TextBoxState state = sample_box(40);
    state.text = "hello hello hello\nworld";
    const std::string original = state.text;
    const int wrapped = text_box_line_count(state);
    state.box.w = 1600;
    const int single = text_box_line_count(state);
    expect(state.text == original, "reflow kept the pasted newline");
    expect(wrapped > single, "the long paragraph wraps when the box is narrow");
  }

  // Resize changes width; the string is not edited.
  {
    TextBoxState state = sample_box(400);
    const std::string original = state.text;
    const int before = text_box_line_count(state);
    const Rect start = state.box;
    const int grab_x = start.x + start.w;
    resize_text_box(state.box, TextBoxHit::East, grab_x - 360, start.y, start, grab_x, start.y);
    expect(state.box.w < start.w, "east handle shrinks the box");
    expect(state.box.h == start.h, "east handle keeps the height");
    expect(state.box.x == start.x, "east handle keeps the left edge");
    expect(state.text == original, "resize does not edit the text");
    expect(text_box_line_count(state) > before, "shrinking the box reflows");
    const Rect moved_from = state.box;
    move_text_box(state.box, 12, -4);
    expect(state.box.x == moved_from.x + 12 && state.box.y == moved_from.y - 4, "border drag moves");
    expect(state.box.w == moved_from.w && state.box.h == moved_from.h, "move keeps the size");
    expect(state.text == original, "move does not edit the text");
  }

  // Minimum size while dragging a handle past the opposite edge.
  {
    const Rect start{10, 10, 80, 50};
    Rect box = start;
    resize_text_box(box, TextBoxHit::West, 1000, 10, start, start.x, start.y);
    expect(box.w == lundukepaint::kTextBoxMinSize, "west drag clamps to the minimum width");
    expect(box.x + box.w == start.x + start.w, "west drag keeps the right edge");
  }

  // Hit regions: handles, border, inside, and click-away.
  {
    const Rect box{10, 20, 100, 80};
    const int radius = text_box_handle_radius(1.0);
    expect(radius >= 4 && radius <= 8, "zoom-1 handle radius stays grabbable");
    expect(text_box_border_slop(1.0) < radius, "handles win over the border band");
    expect(hit_test_text_box(box, 10, 20, 1.0) == TextBoxHit::NorthWest, "corner handle");
    expect(hit_test_text_box(box, 60, 20, 1.0) == TextBoxHit::North, "top midpoint handle");
    expect(hit_test_text_box(box, 110, 60, 1.0) == TextBoxHit::East, "right midpoint handle");
    expect(hit_test_text_box(box, 35, 20, 1.0) == TextBoxHit::Border, "border between handles");
    expect(hit_test_text_box(box, 10, 40, 1.0) == TextBoxHit::Border, "side border between handles");
    expect(hit_test_text_box(box, 60, 60, 1.0) == TextBoxHit::Inside, "click inside the box");
    expect(hit_test_text_box(box, 0, 0, 1.0) == TextBoxHit::Outside, "click away from the box");
    expect(text_box_press_action(TextBoxHit::Outside) == TextBoxPress::Commit, "click-away commits");
    expect(text_box_press_action(TextBoxHit::Inside) == TextBoxPress::PlaceCursor, "inside places the caret");
    expect(text_box_press_action(TextBoxHit::Border) == TextBoxPress::Move, "border moves");
    expect(text_box_press_action(TextBoxHit::SouthEast) == TextBoxPress::Resize, "handle resizes");
    // A point 5px from the corner is a handle at 1x and not at a high zoom.
    expect(hit_test_text_box(box, 15, 20, 1.0) == TextBoxHit::NorthWest, "low zoom enlarges the handle");
    expect(hit_test_text_box(box, 15, 20, 8.0) != TextBoxHit::NorthWest, "high zoom shrinks the handle");
  }

  // Typing does not invent wrap breaks.
  {
    TextBoxState state;
    text_box_insert(state, "ab");
    expect(state.text == "ab" && state.cursor == 2, "insert appends");
    text_box_move_left(state);
    text_box_insert(state, "X");
    expect(state.text == "aXb" && state.cursor == 2, "insert at the caret");
    text_box_backspace(state);
    expect(state.text == "ab" && state.cursor == 1, "backspace removes one character");
    expect(state.text.find('\n') == std::string::npos, "editing did not insert a wrap break");
  }

  // Changing style does not touch the document. Commit is one undo step.
  {
    auto doc = Document::create(180, 80, Color::white());
    const std::vector<std::uint8_t> original = dump(doc->layers().active_layer());
    TextBoxState state = sample_box(150);
    state.box = Rect{4, 4, 160, 64};
    state.text = "Hello";
    state.color = Color::black();
    state.bold = true;
    state.size_pt = 22;
    expect(dump(doc->layers().active_layer()) == original, "style setup did not stamp");
    expect(doc->history().count() == 0, "style setup pushed no history");
    expect(!doc->dirty(), "style setup left the document clean");

    std::vector<std::uint8_t> black;
    std::vector<std::uint8_t> red;
    int bw = 0;
    int bh = 0;
    int rw = 0;
    int rh = 0;
    expect(render_text_box(state, black, bw, bh, nullptr), "black render produced pixels");
    state.color = Color{220, 20, 20, 255};
    expect(render_text_box(state, red, rw, rh, nullptr), "red render produced pixels");
    expect(black != red, "color change changes the raster, not the canvas");
    expect(dump(doc->layers().active_layer()) == original, "recolor did not stamp");
    state.color = Color::black();

    expect(commit_text_box(*doc, state), "commit stamps the text");
    expect(doc->history().count() == 1, "one history entry for the stamp");
    expect(doc->history().name_at(0) == "Text", "history name is Text");
    expect(dump(doc->layers().active_layer()) != original, "stamp changed pixels");
    expect(doc->dirty(), "stamp dirties the document");

    const std::vector<std::uint8_t> stamped = dump(doc->layers().active_layer());
    doc->undo();
    expect(dump(doc->layers().active_layer()) == original, "undo restores the canvas");
    expect(!doc->dirty(), "undo returns to the clean document");
    doc->redo();
    expect(dump(doc->layers().active_layer()) == stamped, "redo restores the stamp");

    TextBoxState empty = state;
    empty.text.clear();
    const int count = doc->history().count();
    expect(!commit_text_box(*doc, empty), "empty text does not stamp");
    expect(doc->history().count() == count, "cancel-sized empty commit adds no history");
    expect(dump(doc->layers().active_layer()) == stamped, "empty commit leaves pixels");
  }

  // Not calling commit (Esc) leaves the canvas unchanged. Checked directly:
  // the stamp function is the only writer.
  {
    auto doc = Document::create(40, 20, Color::white());
    const std::vector<std::uint8_t> original = dump(doc->layers().active_layer());
    TextBoxState state = sample_box(30);
    state.text = "Nope";
    TextBoxMetrics metrics;
    layout_text_box(state, metrics);
    expect(metrics.line_count >= 1, "cancelled edit still has a layout");
    expect(dump(doc->layers().active_layer()) == original, "layout is not a stamp");
    expect(doc->history().count() == 0, "cancelled edit has no history");
  }

  // Open-dialog preview: one decoded image, nothing for a non-image.
  {
    const std::string png = temp_path(".png");
    const std::string jpeg = temp_path(".jpg");
    const std::string bmp = temp_path(".bmp");
    const std::string text = temp_path(".txt");
    const std::string bogus = temp_path(".png");
    const std::string ora = temp_path(".ora");
    std::vector<std::uint8_t> rgba(8 * 4 * 4, 255);
    rgba[0] = 255;
    rgba[1] = 0;
    rgba[2] = 0;
    rgba[3] = 255;
    std::string error;
    expect(save_flat_image(png, ImageFormat::Png, rgba.data(), 8, 4, 32, 90, error), "write png");
    expect(save_flat_image(jpeg, ImageFormat::Jpeg, rgba.data(), 8, 4, 32, 90, error), "write jpeg");
    expect(save_flat_image(bmp, ImageFormat::Bmp, rgba.data(), 8, 4, 32, 90, error), "write bmp");
    expect(write_bytes(text, "hello", 5), "write text");
    expect(write_bytes(bogus, "not a png", 9), "write bogus png");

    LoadedImage preview;
    expect(load_image_preview(png, 240, preview), "png preview");
    expect(preview.width == 8 && preview.height == 4, "small png is not upscaled");
    expect(load_image_preview(jpeg, 240, preview), "jpeg preview");
    expect(load_image_preview(bmp, 240, preview), "bmp preview");
    expect(!load_image_preview(text, 240, preview), "text file has no preview");
    expect(!load_image_preview(bogus, 240, preview), "unreadable png has no preview");
    expect(!load_image_preview("", 240, preview), "empty path has no preview");

    std::vector<std::uint8_t> big(80 * 40 * 4, 255);
    const std::string big_png = temp_path(".png");
    expect(save_flat_image(big_png, ImageFormat::Png, big.data(), 80, 40, 80 * 4, 90, error),
           "write large png");
    expect(load_image_preview(big_png, 20, preview), "scaled png preview");
    expect(preview.width <= 20 && preview.height <= 20, "preview fits the edge cap");
    expect(preview.width > 0 && preview.height > 0, "scaled preview is non-empty");

    auto doc = Document::create(6, 4, Color::white());
    doc->layers().active_layer().set_pixel(1, 1, Color{0, 180, 0, 255});
    expect(save_ora(ora, *doc, error), "write ora");
    expect(load_image_preview(ora, 240, preview), "ora preview");
    expect(preview.width == 6 && preview.height == 4, "ora preview uses the merged image");

    static const unsigned char kGif[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b};
    const std::string gif = temp_path(".gif");
    expect(write_bytes(gif, kGif, sizeof(kGif)), "write gif");
    expect(load_image_preview(gif, 32, preview), "gif preview");
    expect(preview.width == 1 && preview.height == 1, "gif preview size");

    unlink(png.c_str());
    unlink(jpeg.c_str());
    unlink(bmp.c_str());
    unlink(text.c_str());
    unlink(bogus.c_str());
    unlink(big_png.c_str());
    unlink(ora.c_str());
    unlink(gif.c_str());
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_text_box: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_text_box: ok\n");
  return 0;
}
