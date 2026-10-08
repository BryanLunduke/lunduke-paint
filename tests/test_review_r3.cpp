// SPDX-License-Identifier: GPL-3.0-or-later
//
// Headless regressions for the round-3 review. Display-only checks (status
// bar text and the options rail inside a window) live in test_widgets.

#include "app/live_edit.hpp"
#include "app/shortcut_help.hpp"
#include "doc/commands_layers.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "io/crash_recovery.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"
#include "raster/transform.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include <gdk/gdkkeysyms.h>
#include <glib.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using lundukepaint::AddLayerCommand;
using lundukepaint::CanvasEvent;
using lundukepaint::CloseAnswer;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::ImageFormat;
using lundukepaint::KeepOraDecision;
using lundukepaint::Layer;
using lundukepaint::LiveEditAction;
using lundukepaint::OraSnapshot;
using lundukepaint::Rect;
using lundukepaint::ShortcutHelpRow;
using lundukepaint::Tool;
using lundukepaint::ToolHost;
using lundukepaint::autocrop_bounds;
using lundukepaint::capture_ora_snapshot;
using lundukepaint::create_curve_tool;
using lundukepaint::create_eraser_tool;
using lundukepaint::create_line_tool;
using lundukepaint::create_polygon_tool;
using lundukepaint::create_polyline_tool;
using lundukepaint::create_text_tool;
using lundukepaint::decide_keep_ora;
using lundukepaint::live_edit_for_close;
using lundukepaint::live_edit_for_tab_switch;
using lundukepaint::pattern_at;
using lundukepaint::save_flat_image;
using lundukepaint::shortcut_help_rows;
using lundukepaint::snapshot_layer;

int g_errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_review_r3: %s\n", msg);
    ++g_errors;
  }
}

class StubHost : public ToolHost {
public:
  explicit StubHost(Document& document) : document_(&document) {}
  Document& document() override { return *document_; }
  int stroke_size() const override { return stroke_; }
  void set_stroke_size(int size) override { stroke_ = size < 1 ? 1 : size; }
  bool brush_antialias() const override { return false; }
  void set_brush_antialias(bool) override {}
  int fill_tolerance() const override { return tolerance_; }
  void set_fill_tolerance(int tolerance) override { tolerance_ = tolerance; }
  void invalidate_canvas(Rect) override {}
  void return_to_previous_tool() override {}
  Color sample_canvas(int, int) const override { return Color::white(); }
  void show_status_hint(const char*) override {}
  const lundukepaint::Pattern& active_pattern() const override { return pattern_at(0); }

  int stroke_ = 1;
  int tolerance_ = 0;
  Document* document_ = nullptr;
};

int ink_count(const Document& document) {
  const Layer& layer = document.layers().active_layer();
  int n = 0;
  for (int y = 0; y < layer.height(); ++y) {
    for (int x = 0; x < layer.width(); ++x) {
      if (layer.pixel(x, y) != Color::white()) {
        ++n;
      }
    }
  }
  return n;
}

void fill_black(Document& document) {
  Layer& layer = document.layers().active_layer();
  for (int y = 0; y < layer.height(); ++y) {
    for (int x = 0; x < layer.width(); ++x) {
      layer.set_pixel(x, y, Color::black());
    }
  }
}

void test_stroke_width() {
  auto narrow = Document::create(24, 24, Color::white());
  auto wide = Document::create(24, 24, Color::white());
  StubHost narrow_host(*narrow);
  StubHost wide_host(*wide);
  narrow_host.set_stroke_size(1);
  wide_host.set_stroke_size(8);
  std::unique_ptr<Tool> line_narrow(create_line_tool());
  std::unique_ptr<Tool> line_wide(create_line_tool());
  line_narrow->set_host(&narrow_host);
  line_wide->set_host(&wide_host);
  const CanvasEvent down{2, 12, 1, 0};
  const CanvasEvent up{20, 12, 1, 0};
  line_narrow->on_press(down);
  line_narrow->on_release(up);
  line_wide->on_press(down);
  line_wide->on_release(up);
  const int n1 = ink_count(*narrow);
  const int n8 = ink_count(*wide);
  expect(n1 > 0, "1px line paints");
  expect(n8 > n1, "line width 8 follows the picker, not a private 1px thickness");

  auto erased_thin = Document::create(24, 24, Color::white());
  auto erased_wide = Document::create(24, 24, Color::white());
  fill_black(*erased_thin);
  fill_black(*erased_wide);
  StubHost thin_host(*erased_thin);
  StubHost erase_host(*erased_wide);
  thin_host.set_stroke_size(1);
  erase_host.set_stroke_size(8);
  std::unique_ptr<Tool> eraser_thin(create_eraser_tool());
  std::unique_ptr<Tool> eraser_wide(create_eraser_tool());
  eraser_thin->set_host(&thin_host);
  eraser_wide->set_host(&erase_host);
  const CanvasEvent dab{12, 12, 1, 0};
  eraser_thin->on_press(dab);
  eraser_thin->on_release(dab);
  eraser_wide->on_press(dab);
  eraser_wide->on_release(dab);
  expect(ink_count(*erased_wide) < ink_count(*erased_thin),
         "eraser width follows the picker");
}

void test_autocrop() {
  std::vector<std::uint8_t> solid(4 * 4 * 4, 255);
  const Rect nothing = autocrop_bounds(solid.data(), 4, 4, 16);
  expect(nothing.empty(), "a uniform image autocrops to nothing");
  solid[0] = 0;
  const Rect spot = autocrop_bounds(solid.data(), 4, 4, 16);
  expect(!spot.empty(), "one different pixel is still content");
}

void test_persistent_preview() {
  auto doc = Document::create(32, 32, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> polygon(create_polygon_tool());
  polygon->set_host(&host);
  polygon->on_press(CanvasEvent{2, 2, 1, 0});
  polygon->on_press(CanvasEvent{20, 2, 1, 0});
  expect(polygon->has_uncommitted_preview(), "two polygon vertices are an open shape");
  expect(doc->unsaved_overlay() && doc->dirty(), "an open polygon dirties the document");
  polygon->on_press(CanvasEvent{20, 20, 1, 0});
  expect(polygon->on_commit(), "commit finishes the polygon");
  expect(!doc->unsaved_overlay(), "a finished polygon clears the overlay");
  expect(doc->history().can_undo(), "the polygon is one undo step");
  expect(ink_count(*doc) > 0, "the polygon pixels survived the commit");

  auto line_doc = Document::create(32, 32, Color::white());
  StubHost line_host(*line_doc);
  std::unique_ptr<Tool> polyline(create_polyline_tool());
  polyline->set_host(&line_host);
  polyline->on_press(CanvasEvent{1, 1, 1, 0});
  polyline->on_press(CanvasEvent{12, 8, 1, 0});
  expect(line_doc->unsaved_overlay(), "an open polyline dirties the document");
  polyline->on_cancel();
  expect(!line_doc->unsaved_overlay(), "escape clears the polyline overlay");
  expect(!line_doc->history().can_undo(), "cancel does not commit the polyline");

  auto curve_doc = Document::create(32, 32, Color::white());
  StubHost curve_host(*curve_doc);
  std::unique_ptr<Tool> curve(create_curve_tool());
  curve->set_host(&curve_host);
  curve->on_press(CanvasEvent{2, 16, 1, 0});
  curve->on_motion(CanvasEvent{24, 16, 0, 0});
  curve->on_release(CanvasEvent{24, 16, 1, 0});
  expect(curve->has_uncommitted_preview() && curve_doc->unsaved_overlay(),
         "a curve with endpoints is still uncommitted");
  expect(curve->on_commit(), "commit finishes the curve");
  expect(!curve_doc->unsaved_overlay() && curve_doc->history().can_undo(),
         "the curve lands in history and drops the overlay");
}

void test_text_tab_policy() {
  auto doc = Document::create(80, 40, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> text(create_text_tool());
  text->set_host(&host);
  text->on_press(CanvasEvent{4, 4, 1, 0});
  expect(text->captures_keys(), "a click opens the text box");
  expect(!doc->unsaved_overlay(), "an empty text box is not unsaved work");
  expect(text->on_key(GDK_KEY_H, 0, "H"), "typing goes into the box");
  expect(doc->unsaved_overlay() && doc->dirty(), "typed text dirties the document");
  doc->set_layer_locked(0, true);
  expect(!text->on_commit(), "a locked layer refuses the stamp");
  expect(text->captures_keys(), "the text box stays up when the stamp fails");
  doc->set_layer_locked(0, false);
  expect(text->on_commit(), "an unlocked layer stamps the text");
  expect(!text->captures_keys() && !doc->unsaved_overlay(), "a successful stamp closes the box");
  expect(doc->history().can_undo(), "stamped text is one undo step");

  std::vector<std::uint8_t> copy(static_cast<std::size_t>(doc->layers().active_layer().stride()) *
                                 static_cast<std::size_t>(doc->height()),
                                 255);
  std::unique_ptr<Tool> again(create_text_tool());
  again->set_host(&host);
  again->on_press(CanvasEvent{4, 4, 1, 0});
  again->on_key(GDK_KEY_Z, 0, "Z");
  expect(again->paint_recovery_overlay(0, copy.data(), doc->width(), doc->height(), doc->width() * 4),
         "recovery can paint the open text box");
  bool ink = false;
  for (std::uint8_t byte : copy) {
    if (byte != 255) {
      ink = true;
      break;
    }
  }
  expect(ink, "the recovery image includes the open text");
  again->on_cancel();
}

void test_recovery_files() {
  char dir[] = "/tmp/lunduke-paint-r3-XXXXXX";
  expect(mkdtemp(dir) != nullptr, "recovery temp dir");
  g_setenv("XDG_STATE_HOME", dir, TRUE);
  auto a = Document::create(4, 4, Color::white());
  auto b = Document::create(4, 4, Color::white());
  a->set_dirty(true);
  b->set_dirty(true);
  const std::string path_a = lundukepaint::crash_recovery::autosave_path_for(a->recovery_id());
  const std::string path_b = lundukepaint::crash_recovery::autosave_path_for(b->recovery_id());
  expect(path_a != path_b, "each document has its own recovery path");
  std::string error;
  expect(lundukepaint::crash_recovery::write_document_file(*a, path_a, error), "write tab A");
  expect(lundukepaint::crash_recovery::write_document_file(*b, path_b, error), "write tab B");
  lundukepaint::crash_recovery::clear_file(path_a);
  expect(g_file_test(path_b.c_str(), G_FILE_TEST_IS_REGULAR) == TRUE,
         "saving one tab leaves the other recovery file");
  expect(lundukepaint::crash_recovery::recovery_tick_should_run(true, true, true, true, true),
         "a stroke or preview does not skip autosave of committed pixels");
  expect(!lundukepaint::crash_recovery::recovery_snapshot_allowed(true),
         "a live effect preview is still excluded from the snapshot");

  auto painted = Document::create(8, 8, Color::white());
  painted->layers().active_layer().set_pixel(3, 3, Color{200, 0, 0, 255});
  painted->set_dirty(true);
  StubHost host(*painted);
  std::unique_ptr<Tool> polygon(create_polygon_tool());
  polygon->set_host(&host);
  polygon->on_press(CanvasEvent{1, 1, 1, 0});
  polygon->on_press(CanvasEvent{6, 1, 1, 0});
  OraSnapshot snapshot = capture_ora_snapshot(*painted, false);
  expect(!snapshot.layers.empty(), "snapshot has the layer");
  const Color kept = [&]() {
    const auto& item = snapshot.layers[0];
    const std::uint8_t* p = item.pixels.data() + static_cast<std::size_t>(3) * 4 +
                            static_cast<std::size_t>(3) * static_cast<std::size_t>(item.stride);
    return Color{p[0], p[1], p[2], p[3]};
  }();
  expect(kept.r == 200, "committed pixels are snapshotted during an open polygon");
}

void test_selection_undo_and_lock() {
  auto doc = Document::create(16, 16, Color::white());
  Layer& layer = doc->layers().active_layer();
  layer.set_pixel(1, 1, Color::black());
  doc->selection().set_rect(Rect{1, 1, 1, 1});
  expect(doc->selection().lift(layer, 0), "lift the pixel");
  doc->selection().move_float(5, 6);
  expect(doc->commit_floating(), "stamp the move");
  expect(!doc->selection().floating(), "stamp drops the float");
  expect(layer.pixel(5, 6) == Color::black(), "moved pixel is at the destination");
  doc->undo();
  expect(doc->selection().floating(), "undo restores the floating selection");
  expect(doc->selection().float_x() == 5 && doc->selection().float_y() == 6,
         "undo restores the moved geometry");
  expect(layer.pixel(1, 1) == Color::black(), "undo puts the source pixel back");
  doc->redo();
  expect(!doc->selection().floating(), "redo stamps again");
  expect(layer.pixel(5, 6) == Color::black(), "redo restores the destination pixel");

  auto stack = Document::create(8, 8, Color::white());
  expect(stack->add_layer(), "second layer");
  expect(stack->set_active_layer(0), "back to the background");
  stack->layers().active_layer().set_pixel(2, 2, Color::black());
  stack->selection().set_rect(Rect{2, 2, 1, 1});
  expect(stack->selection().lift(stack->layers().active_layer(), 0), "lift on layer 0");
  stack->set_layer_locked(0, true);
  std::string blocked;
  stack->set_on_blocked([&blocked](const char* message) { blocked = message != nullptr ? message : ""; });
  expect(!stack->set_active_layer(1), "switching layers aborts while the float is stuck");
  expect(stack->layers().active_index() == 0, "the active layer stays put");
  expect(stack->selection().floating(), "the float is not dropped");
  expect(!blocked.empty(), "the lock is reported");
  expect(!stack->add_layer(), "adding a layer also aborts");

  stack->set_layer_locked(0, false);
  expect(stack->selection().source_layer() == 0, "source starts at 0");
  stack->commit(std::make_unique<AddLayerCommand>(0, snapshot_layer(stack->layers().at(0))));
  expect(stack->selection().source_layer() == 1, "inserting below remaps the float");
  stack->undo();
  expect(stack->selection().source_layer() == 0, "undoing the insert restores the source layer");
}

void test_close_and_keep_ora() {
  expect(live_edit_for_close(CloseAnswer::Cancel) == LiveEditAction::Leave,
         "quit Cancel leaves the text box");
  expect(live_edit_for_close(CloseAnswer::Save) == LiveEditAction::Stamp, "quit Save stamps");
  expect(live_edit_for_close(CloseAnswer::Discard) == LiveEditAction::Cancel, "quit Discard drops it");
  expect(live_edit_for_tab_switch(true, true, false) == LiveEditAction::Stamp,
         "a tab switch stamps typed text");
  expect(live_edit_for_tab_switch(true, false, false) == LiveEditAction::Cancel,
         "a tab switch closes an empty text box");
  expect(live_edit_for_tab_switch(false, false, true) == LiveEditAction::Stamp,
         "a tab switch stamps an open polygon");
  const KeepOraDecision failed = decide_keep_ora(true, true, false);
  expect(!failed.adopt_path && !failed.mark_clean && !failed.clear_recovery,
         "a failed .ora copy leaves the document dirty");
  const KeepOraDecision ok = decide_keep_ora(true, true, true);
  expect(ok.adopt_path && ok.mark_clean && ok.clear_recovery, "both files written adopts the flat path");
}

void test_shortcuts_and_mnemonics() {
  int count = 0;
  const ShortcutHelpRow* rows = shortcut_help_rows(count);
  int u_rows = 0;
  bool deselect = false;
  bool escape = false;
  bool spray = false;
  for (int i = 0; i < count; ++i) {
    const std::string keys = rows[i].keys;
    if (keys == "U") {
      ++u_rows;
    }
    if (std::strcmp(rows[i].action, "Deselect") == 0) {
      deselect = keys == "Ctrl+D";
    }
    if (std::strcmp(rows[i].action, "Escape") == 0) {
      escape = keys.find("deselects only when nothing is in progress") != std::string::npos;
    }
    if (std::strcmp(rows[i].action, "Spray / Polyline") == 0) {
      spray = keys == "Y / N";
    }
    expect(keys.find(" / U") == std::string::npos && keys.find("U /") == std::string::npos,
           "U is not shared with spray");
  }
  expect(u_rows == 1, "one rounded-rectangle shortcut row");
  expect(deselect, "deselect is Ctrl+D");
  expect(escape, "escape cancels in-progress work before deselect");
  expect(spray, "spray and polyline are Y and N");

  const std::string root = REVIEW_R3_SOURCE;
  std::ifstream menus(root + "/data/ui/menus.xml");
  std::string menu((std::istreambuf_iterator<char>(menus)), std::istreambuf_iterator<char>());
  expect(menu.find("Dese_lect") != std::string::npos, "Deselect mnemonic avoids Delete");
  expect(menu.find("Pre_ferences") != std::string::npos, "Preferences mnemonic avoids Paste");
  expect(menu.find("Lo_wer Layer") != std::string::npos, "Lower Layer mnemonic avoids Delete Layer");
  expect(menu.find(">_Deselect<") == std::string::npos, "old Deselect mnemonic is gone");
  expect(menu.find(">_Preferences") == std::string::npos, "old Preferences mnemonic is gone");
  expect(menu.find(">_Lower Layer<") == std::string::npos, "old Lower Layer mnemonic is gone");
  std::ifstream panel(root + "/src/ui/layers_panel.cpp");
  std::string panel_src((std::istreambuf_iterator<char>(panel)), std::istreambuf_iterator<char>());
  expect(panel_src.find("\"Lo_wer\"") != std::string::npos, "layer menu uses Lo_wer");
  expect(panel_src.find("\"_Lower\"") == std::string::npos, "layer menu no longer uses _Lower");
}

void test_save_errno() {
  char dir[] = "/tmp/lunduke-paint-r3-save-XXXXXX";
  expect(mkdtemp(dir) != nullptr, "save temp dir");
  const std::string blocked = std::string(dir) + "/locked";
  expect(mkdir(blocked.c_str(), 0755) == 0, "locked dir");
  expect(chmod(blocked.c_str(), 0555) == 0, "drop write permission");
  std::vector<std::uint8_t> px(4 * 4, 255);
  std::string error;
  const bool saved =
      save_flat_image(blocked + "/x.png", ImageFormat::Png, px.data(), 1, 1, 4, 90, error);
  expect(!saved, "save into a read-only directory fails");
  expect(error.find(':') != std::string::npos, "the save error includes an OS reason");
  expect(error.find("Permission denied") != std::string::npos ||
             error.find("Read-only") != std::string::npos,
         "the OS text names the permission failure");
  chmod(blocked.c_str(), 0755);
}

}  // namespace

int main() {
  test_stroke_width();
  test_autocrop();
  test_persistent_preview();
  test_text_tab_policy();
  test_recovery_files();
  test_selection_undo_and_lock();
  test_close_and_keep_ora();
  test_shortcuts_and_mnemonics();
  test_save_errno();
  if (g_errors != 0) {
    std::fprintf(stderr, "test_review_r3: %d failure(s)\n", g_errors);
    return 1;
  }
  std::printf("test_review_r3: ok\n");
  return 0;
}
