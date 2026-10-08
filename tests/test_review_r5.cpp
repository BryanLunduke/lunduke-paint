// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-5 review. Each finding is one case, and the case covers the whole
// class (every float origin, every masked transform and paste) rather than
// the single call the review happened to hit.

#include "app/live_edit.hpp"
#include "app/shortcut_help.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/selection.hpp"
#include "io/crash_recovery.hpp"
#include "tools/selection_xform.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gtkmm/main.h>

#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using lundukepaint::CanvasEvent;
using lundukepaint::CloseAnswer;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::EscapeTarget;
using lundukepaint::Layer;
using lundukepaint::LivePath;
using lundukepaint::Rect;
using lundukepaint::Selection;
using lundukepaint::SelectionState;
using lundukepaint::SelectionXform;
using lundukepaint::SettleResult;
using lundukepaint::Tool;
using lundukepaint::ToolHost;
using lundukepaint::copy_merged_rgba;
using lundukepaint::copy_selection_rgba;
using lundukepaint::create_text_tool;
using lundukepaint::escape_target;
using lundukepaint::plan_close_save;
using lundukepaint::settle_tool_live;

int g_errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_review_r5: %s\n", msg);
    ++g_errors;
  }
}

const Color kBlue{20, 40, 200, 255};
const Color kRed{200, 30, 30, 255};
const Color kGreen{10, 180, 40, 255};
const Color kYellow{240, 220, 20, 255};

constexpr int kBox = 6;
constexpr int kOx = 4;
constexpr int kOy = 4;

void paint(Layer& layer, int x, int y, int w, int h, Color color) {
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      if (xx >= 0 && yy >= 0 && xx < layer.width() && yy < layer.height()) {
        layer.set_pixel(xx, yy, color);
      }
    }
  }
}

std::vector<std::uint8_t> corner_mask() {
  std::vector<std::uint8_t> mask(static_cast<std::size_t>(kBox) * kBox, 0);
  mask[0] = 1;
  return mask;
}

void paint_lasso_scene(Document& doc) {
  Layer& layer = doc.layers().active_layer();
  paint(layer, 16, 4, 12, 12, kRed);
  paint(layer, kOx, kOy, kBox, kBox, kBlue);
  layer.set_pixel(kOx, kOy, Color::black());
  layer.set_pixel(3, 4, kGreen);
}

void arm_lasso(Document& doc) {
  paint_lasso_scene(doc);
  doc.selection().set_mask({kOx, kOy, kBox, kBox}, corner_mask());
}

std::vector<std::uint8_t> snapshot(const Layer& layer) {
  const std::size_t n = static_cast<std::size_t>(layer.stride()) * static_cast<std::size_t>(layer.height());
  return std::vector<std::uint8_t>(layer.pixels(), layer.pixels() + n);
}

void expect_float_roundtrip(const char* name, Document& doc) {
  char buf[160];
  std::snprintf(buf, sizeof(buf), "%s is floating", name);
  expect(doc.selection().floating(), buf);
  std::snprintf(buf, sizeof(buf), "%s marks the document dirty", name);
  expect(doc.dirty(), buf);
  std::snprintf(buf, sizeof(buf), "%s is stored by crash recovery", name);
  expect(lundukepaint::crash_recovery::recovery_tick_should_run(doc.dirty(), false, false, false, true),
         buf);
  doc.mark_clean();
  std::snprintf(buf, sizeof(buf), "%s stays dirty after mark-clean", name);
  expect(doc.dirty(), buf);

  const SelectionState live = doc.selection().capture();
  const int history = doc.history().count();
  std::snprintf(buf, sizeof(buf), "%s commits", name);
  expect(doc.commit_floating("Place"), buf);
  std::snprintf(buf, sizeof(buf), "%s commit is a history step", name);
  expect(doc.history().count() == history + 1, buf);
  std::snprintf(buf, sizeof(buf), "%s commit drops the live float", name);
  expect(!doc.selection().floating(), buf);

  doc.undo();
  std::snprintf(buf, sizeof(buf), "%s undo restores the float", name);
  expect(doc.selection().floating(), buf);
  expect(doc.selection().float_x() == live.float_x && doc.selection().float_y() == live.float_y, buf);
  expect(doc.selection().float_w() == live.float_w && doc.selection().float_h() == live.float_h, buf);
  expect(doc.selection().copy_mode() == live.copy_mode, buf);
  expect(doc.selection().float_pixels() != nullptr, buf);
  const std::size_t bytes = static_cast<std::size_t>(live.float_w) * static_cast<std::size_t>(live.float_h) * 4;
  expect(std::memcmp(doc.selection().float_pixels(), live.float_pixels.data(), bytes) == 0, buf);
  if (!live.float_coverage.empty()) {
    expect(doc.selection().has_float_coverage(), buf);
    expect(std::memcmp(doc.selection().float_coverage(), live.float_coverage.data(),
                       live.float_coverage.size()) == 0,
           buf);
  }

  doc.redo();
  std::snprintf(buf, sizeof(buf), "%s redo places the float again", name);
  expect(!doc.selection().floating(), buf);
  expect(doc.dirty(), buf);
}

void test_float_origins() {
  {
    auto doc = Document::create(32, 32, Color::white());
    std::vector<std::uint8_t> rgba(2 * 2 * 4, 0);
    rgba[3] = 255;
    doc->paste_floating(8, 8, 2, 2, rgba);
    expect_float_roundtrip("paste", *doc);
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    paint(doc->layers().active_layer(), 2, 2, 4, 4, Color::black());
    doc->selection().set_rect({2, 2, 4, 4});
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "move lifts");
    doc->selection().move_float(18, 8);
    expect_float_roundtrip("move", *doc);
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    paint(doc->layers().active_layer(), 2, 2, 4, 4, Color::black());
    doc->selection().set_rect({2, 2, 4, 4});
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "drag-in lifts");
    doc->selection().set_copy_mode(true);
    doc->selection().move_float(18, 8);
    expect_float_roundtrip("drag-in", *doc);
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    paint(doc->layers().active_layer(), 2, 2, 2, 2, Color::black());
    doc->selection().set_rect({2, 2, 2, 2});
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "transform lifts");
    Selection& sel = doc->selection();
    sel.scale_float_nearest(18, 8, 4, 4, sel.float_pixels(), sel.float_w(), sel.float_h(),
                            sel.float_coverage());
    expect_float_roundtrip("transform", *doc);
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    paint(doc->layers().active_layer(), 2, 2, 4, 4, Color::black());
    doc->selection().set_rect({2, 2, 4, 4});
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "nudge lifts");
    doc->nudge_floating(6, 0);
    expect(doc->selection().float_x() == 8, "nudge moves the float");
    expect_float_roundtrip("nudge", *doc);
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    const auto before = snapshot(doc->layers().active_layer());
    std::vector<std::uint8_t> rgba(2 * 2 * 4, 255);
    doc->paste_floating(-8, -8, 2, 2, rgba);
    expect(doc->selection().float_x() == -8 && doc->selection().float_y() == -8, "margin float sits outside");
    expect_float_roundtrip("margin", *doc);
    expect(snapshot(doc->layers().active_layer()) == before, "a margin float never writes the layer");
  }
}

void expect_pixel(const Document& doc, int x, int y, Color color, const char* msg) {
  expect(doc.layers().active_layer().pixel(x, y) == color, msg);
}

void commit_moved(Document& doc, int x, int y, int w, int h, int steps) {
  Selection& sel = doc.selection();
  const std::vector<std::uint8_t> src(
      sel.float_pixels(),
      sel.float_pixels() + static_cast<std::size_t>(sel.float_w()) * static_cast<std::size_t>(sel.float_h()) * 4);
  std::vector<std::uint8_t> cov;
  const std::uint8_t* cov_ptr = nullptr;
  if (sel.has_float_coverage()) {
    cov.assign(sel.float_coverage(),
               sel.float_coverage() + static_cast<std::size_t>(sel.float_w()) * static_cast<std::size_t>(sel.float_h()));
    cov_ptr = cov.data();
  }
  if (steps == 0) {
    sel.scale_float_nearest(x, y, w, h, src.data(), kBox, kBox, cov_ptr);
  } else {
    sel.rotate_float_steps(steps, x, y, src.data(), kBox, kBox, cov_ptr);
  }
  expect(doc.commit_floating("Place"), "masked transform commits");
}

void test_mask_carries() {
  {
    auto doc = Document::create(32, 32, Color::white());
    arm_lasso(*doc);
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint8_t> coverage;
    copy_selection_rgba(doc->layers().active_layer(), doc->selection(), doc->width(), doc->height(), w, h,
                        rgba, &coverage);
    expect(w == kBox && h == kBox, "copy keeps the lasso bounds");
    expect(!coverage.empty() && coverage[0] == 1, "copy covers the lasso pixel");
    expect(coverage[1] == 0, "copy leaves the neighbor out of the mask");
    expect(rgba[3] == 255 && rgba[0] == 0, "copy keeps the covered pixel");
    expect(rgba[4] == 0 && rgba[7] == 0, "copy does not take the uncovered neighbor");

    doc->delete_selection();
    expect_pixel(*doc, kOx, kOy, Color::transparent(), "cut punches only the lasso pixel");
    expect_pixel(*doc, kOx + 1, kOy, kBlue, "cut leaves the rest of the bounds");
    expect_pixel(*doc, 3, 4, kGreen, "cut leaves the pixel outside the bounds");

    doc->paste_floating(16, 4, w, h, rgba, coverage);
    expect(doc->selection().float_covers(0, 0) && !doc->selection().float_covers(1, 0),
           "paste keeps the lasso mask");
    expect(doc->commit_floating("Paste"), "paste commits");
    expect_pixel(*doc, 16, 4, Color::black(), "paste writes the covered pixel");
    expect_pixel(*doc, 17, 4, kRed, "paste does not punch the uncovered neighbor");
  }

  {
    auto doc = Document::create(32, 32, Color::white());
    paint(doc->layers().active_layer(), kOx, kOy, kBox, kBox, kYellow);
    expect(doc->add_layer(), "copy-merged adds a layer");
    doc->layers().active_layer().set_pixel(kOx, kOy, Color::black());
    doc->selection().set_mask({kOx, kOy, kBox, kBox}, corner_mask());
    int w = 0;
    int h = 0;
    std::vector<std::uint8_t> rgba;
    std::vector<std::uint8_t> coverage;
    copy_merged_rgba(doc->layers(), doc->selection(), doc->width(), doc->height(), w, h, rgba, &coverage);
    expect(w == kBox && h == kBox && coverage.size() == static_cast<std::size_t>(w * h),
           "copy-merged returns the lasso bounds");
    expect(coverage[0] == 1 && rgba[0] == 0 && rgba[3] == 255, "copy-merged keeps the covered pixel");
    expect(coverage[1] == 0 && rgba[4] == 0 && rgba[7] == 0,
           "copy-merged does not fill the lasso with its bounding box");

    auto pasted = Document::from_masked_paste(w, h, rgba.data(), coverage.data());
    expect(pasted->width() == kBox && pasted->height() == kBox, "paste-into-new uses the copied size");
    expect(pasted->layers().active_layer().pixel(0, 0) == Color::black(),
           "paste-into-new keeps the lasso pixel");
    expect(pasted->layers().active_layer().pixel(1, 0) == Color::transparent(),
           "paste-into-new leaves the uncovered neighbor empty");
  }

  {
    auto doc = Document::create(32, 32, Color::white());
    arm_lasso(*doc);
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "scale lifts");
    commit_moved(*doc, 16, 4, 12, 12, 0);
    expect_pixel(*doc, kOx, kOy, Color::transparent(), "scale punches only the lifted lasso pixel");
    expect_pixel(*doc, kOx + 1, kOy, kBlue, "scale leaves the rest of the origin");
    expect_pixel(*doc, 16, 4, Color::black(), "scale writes the resampled lasso");
    expect_pixel(*doc, 17, 4, Color::black(), "nearest scale covers the source lasso pixel");
    expect_pixel(*doc, 18, 4, kRed, "scale does not punch outside the resampled mask");
  }

  {
    auto doc = Document::create(32, 32, Color::white());
    arm_lasso(*doc);
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "rotate lifts");
    commit_moved(*doc, 16, 4, kBox, kBox, 1);
    expect_pixel(*doc, kOx, kOy, Color::transparent(), "rotate punches the original lasso pixel");
    expect_pixel(*doc, kOx + 1, kOy, kBlue, "rotate leaves the origin neighbor");
    expect_pixel(*doc, 21, 4, Color::black(), "rotate turns the lasso with the pixels");
    expect_pixel(*doc, 16, 4, kRed, "rotate does not stamp the old bounding box");
  }

  {
    auto doc = Document::create(32, 32, Color::white());
    arm_lasso(*doc);
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "flip lifts");
    doc->selection().flip_horizontal();
    expect(doc->selection().float_covers(kBox - 1, 0) && !doc->selection().float_covers(0, 0),
           "flip moves the coverage with the pixels");
    expect(doc->selection().origin_covers(0, 0) && !doc->selection().origin_covers(1, 0),
           "flip leaves the origin hole as the lasso");
    expect(doc->commit_floating("Flip"), "flip commits");
    expect_pixel(*doc, kOx, kOy, Color::transparent(), "flip punches the original lasso pixel");
    expect_pixel(*doc, kOx + 1, kOy, kBlue, "flip leaves the origin neighbor");
    expect_pixel(*doc, kOx + kBox - 1, kOy, Color::black(), "flip writes the covered pixel in its new place");
  }

  {
    auto doc = Document::create(32, 32, Color::white());
    arm_lasso(*doc);
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "flip-v lifts");
    doc->selection().flip_vertical();
    expect(doc->selection().float_covers(0, kBox - 1) && !doc->selection().float_covers(0, 0),
           "vertical flip moves the coverage");
    expect(doc->commit_floating("Flip"), "vertical flip commits");
    expect_pixel(*doc, kOx, kOy, Color::transparent(), "vertical flip punches the original lasso pixel");
    expect_pixel(*doc, kOx, kOy + 1, kBlue, "vertical flip leaves the origin neighbor");
    expect_pixel(*doc, kOx, kOy + kBox - 1, Color::black(), "vertical flip writes the covered pixel");
  }

  {
    auto doc = Document::create(32, 32, Color::white());
    arm_lasso(*doc);
    expect(doc->selection().lift(doc->layers().active_layer(), 0), "nudge mask lifts");
    doc->nudge_floating(12, 0);
    expect(doc->selection().float_covers(0, 0) && !doc->selection().float_covers(1, 0),
           "nudge keeps the lasso mask");
    expect(doc->commit_floating("Nudge"), "nudge commits");
    expect_pixel(*doc, kOx, kOy, Color::transparent(), "nudge punches only the lasso pixel");
    expect_pixel(*doc, kOx + 1, kOy, kBlue, "nudge leaves the rest of the origin");
    expect_pixel(*doc, 16, 4, Color::black(), "nudge writes the covered pixel");
    expect_pixel(*doc, 17, 4, kRed, "nudge does not punch the neighbor at the destination");
  }

  {
    auto doc = Document::create(32, 32, Color::white());
    arm_lasso(*doc);
    expect(doc->crop_to_selection(), "crop runs");
    expect(doc->width() == kBox && doc->height() == kBox, "crop uses the lasso bounds");
    expect(doc->layers().active_layer().pixel(0, 0) == Color::black(), "crop keeps the lasso pixel");
    expect(doc->layers().active_layer().pixel(1, 0) == Color::transparent(),
           "crop clears the rest of the bounding box");
  }
}

void test_close_save_waits_for_a_name() {
  const auto cancelled = plan_close_save(CloseAnswer::Save, true, false);
  expect(cancelled.keep_edit && !cancelled.commit_then_write && !cancelled.discard_edit,
         "cancelling the chooser leaves the edit");
  const auto named = plan_close_save(CloseAnswer::Save, true, true);
  expect(!named.keep_edit && named.commit_then_write, "an accepted name commits, then writes");
  const auto known = plan_close_save(CloseAnswer::Save, false, false);
  expect(known.commit_then_write, "a document that already has a path commits on Save");
  const auto discard = plan_close_save(CloseAnswer::Discard, true, false);
  expect(discard.discard_edit && !discard.commit_then_write, "Discard drops the edit");
  const auto cancel = plan_close_save(CloseAnswer::Cancel, true, false);
  expect(cancel.keep_edit && !cancel.commit_then_write, "Cancel leaves the edit");
}

class StubHost : public ToolHost {
public:
  explicit StubHost(Document& document) : document_(&document) {}
  Document& document() override { return *document_; }
  int stroke_size() const override { return 1; }
  void set_stroke_size(int) override {}
  bool brush_antialias() const override { return false; }
  void set_brush_antialias(bool) override {}
  int fill_tolerance() const override { return 0; }
  void set_fill_tolerance(int) override {}
  void invalidate_canvas(Rect) override {}
  void return_to_previous_tool() override {}
  Color sample_canvas(int, int) const override { return Color::white(); }
  void show_status_hint(const char*) override {}

  Document* document_ = nullptr;
};

bool gtk_ready() {
  static int state = -1;
  if (state >= 0) {
    return state == 1;
  }
  int argc = 0;
  char** argv = nullptr;
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_review_r5: display required but gtk_init_check failed\n");
      state = 0;
      g_errors += 1;
    } else {
      std::printf("test_review_r5: no display, text cases not constructed\n");
      state = 0;
    }
    return false;
  }
  Gtk::Main::init_gtkmm_internals();
  state = 1;
  return true;
}

void test_blank_text_settles() {
  if (!gtk_ready()) {
    return;
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    StubHost host(*doc);
    std::unique_ptr<Tool> text(create_text_tool());
    text->set_host(&host);
    const int history = doc->history().count();
    text->on_press(CanvasEvent{-400, 4, 1, 0});
    expect(text->on_key(GDK_KEY_H, 0, "H"), "off-canvas text accepts a letter");
    expect(text->on_key(GDK_KEY_i, 0, "i"), "off-canvas text accepts the rest");
    expect(settle_tool_live(text.get(), doc.get(), LivePath::Save) == SettleResult::Proceed,
           "text that misses the picture does not block Save");
    expect(!text->captures_keys(), "the empty text box is closed");
    expect(doc->history().count() == history, "off-canvas text adds no history");
    expect(settle_tool_live(text.get(), doc.get(), LivePath::ToolChange) == SettleResult::Proceed,
           "the next tool is not blocked");
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    doc->set_foreground(Color::white());
    StubHost host(*doc);
    std::unique_ptr<Tool> text(create_text_tool());
    text->set_host(&host);
    text->on_press(CanvasEvent{4, 4, 1, 0});
    expect(text->on_key(GDK_KEY_H, 0, "H"), "matching ink accepts a letter");
    expect(settle_tool_live(text.get(), doc.get(), LivePath::Save) == SettleResult::Proceed,
           "text that matches the layer does not block Save");
    expect(!text->captures_keys(), "matching ink closes the box");
  }
  {
    auto doc = Document::create(32, 32, Color::white());
    StubHost host(*doc);
    std::unique_ptr<Tool> text(create_text_tool());
    text->set_host(&host);
    text->on_press(CanvasEvent{4, 4, 1, 0});
    expect(text->on_key(GDK_KEY_H, 0, "H"), "locked text accepts a letter");
    doc->layers().active_layer().set_locked(true);
    expect(settle_tool_live(text.get(), doc.get(), LivePath::Save) == SettleResult::Blocked,
           "a locked layer still blocks Save");
    expect(text->captures_keys(), "a locked text box stays up");
  }
}

void test_escape_cancels_resting_float() {
  expect(escape_target(false, true) == EscapeTarget::FloatCancel, "Escape cancels a resting float");
  expect(escape_target(true, true) == EscapeTarget::ToolCancel, "Escape during a drag stays with the tool");
  expect(escape_target(false, false) == EscapeTarget::Deselect, "Escape deselects when nothing is open");

  int rows = 0;
  bool found = false;
  const auto* help = lundukepaint::shortcut_help_rows(rows);
  for (int i = 0; i < rows; ++i) {
    if (help[i].keys != nullptr &&
        std::strstr(help[i].keys, "deselects only when nothing is in progress") != nullptr) {
      found = true;
    }
  }
  expect(found, "shortcut help still deselects only when nothing is in progress");

  auto doc = Document::create(32, 32, Color::white());
  arm_lasso(*doc);
  expect(doc->selection().lift(doc->layers().active_layer(), 0), "escape lifts");
  doc->selection().move_float(16, 4);
  const int history = doc->history().count();
  const auto before = snapshot(doc->layers().active_layer());
  expect(doc->cancel_floating(), "escape cancels the float");
  expect(!doc->selection().floating(), "the float is gone");
  expect(doc->selection().has_mask(), "the lasso is restored");
  expect(doc->selection().mask_at(kOx, kOy) && !doc->selection().mask_at(kOx + 1, kOy),
         "the restored mask is still the lasso");
  expect(doc->history().count() == history, "cancelling a float is not a history step");
  expect(snapshot(doc->layers().active_layer()) == before, "cancelling a float does not punch");

  doc->selection().lift(doc->layers().active_layer(), 0);
  doc->selection().set_copy_mode(true);
  doc->selection().move_float(16, 4);
  expect(doc->cancel_floating(), "escape drops a copy");
  expect(doc->selection().empty(), "a cancelled copy leaves no selection");
  expect(snapshot(doc->layers().active_layer()) == before, "a cancelled copy does not punch");

  arm_lasso(*doc);
  expect(doc->selection().lift(doc->layers().active_layer(), 0), "drag lifts");
  StubHost host(*doc);
  SelectionXform xform;
  expect(xform.on_press(&host, CanvasEvent{10, 7, 1, 0}, 1.0), "scale drag starts");
  xform.on_motion(&host, CanvasEvent{22, 7, 0, 0});
  expect(doc->selection().float_w() != kBox, "the drag scaled the float");
  xform.on_cancel(&host);
  expect(doc->selection().float_w() == kBox && doc->selection().float_h() == kBox,
         "Escape during a drag snaps back to the origin size");
  expect(doc->selection().float_x() == kOx && doc->selection().float_y() == kOy,
         "Escape during a drag snaps back to the origin");
  expect(doc->selection().float_covers(0, 0) && !doc->selection().float_covers(1, 0),
         "the snap restores the lasso mask");
}

}  // namespace

int main() {
  test_float_origins();
  test_mask_carries();
  test_close_save_waits_for_a_name();
  test_blank_text_settles();
  test_escape_cancels_resting_float();
  if (g_errors != 0) {
    std::fprintf(stderr, "test_review_r5: %d failure(s)\n", g_errors);
    return 1;
  }
  std::printf("test_review_r5: ok\n");
  return 0;
}
