// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round-4 review. Every live-edit path is run against every kind of live
// state, and the option values that used to diverge are checked against the
// value that actually runs.

#include "app/live_edit.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/selection.hpp"
#include "doc/workspace.hpp"
#include "tools/shape_options.hpp"
#include "tools/tool.hpp"
#include "tools/tools.hpp"

#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/main.h>
#include <gtkmm/spinbutton.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

using lundukepaint::CanvasEvent;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Layer;
using lundukepaint::LiveDisposition;
using lundukepaint::LiveKind;
using lundukepaint::LivePath;
using lundukepaint::SettleResult;
using lundukepaint::Tool;
using lundukepaint::ToolHost;
using lundukepaint::Workspace;
using lundukepaint::classify_live;
using lundukepaint::composite_floating_into_buffer;
using lundukepaint::create_ellipse_fill_tool;
using lundukepaint::create_ellipse_select_tool;
using lundukepaint::create_ellipse_tool;
using lundukepaint::create_fill_tool;
using lundukepaint::create_lasso_tool;
using lundukepaint::create_line_tool;
using lundukepaint::create_magic_wand_tool;
using lundukepaint::create_pencil_tool;
using lundukepaint::create_polygon_tool;
using lundukepaint::create_rect_select_tool;
using lundukepaint::create_rectangle_tool;
using lundukepaint::create_rounded_rect_fill_tool;
using lundukepaint::create_rounded_rect_tool;
using lundukepaint::create_text_tool;
using lundukepaint::live_edit_blocks_silent_close;
using lundukepaint::live_edit_disposition;
using lundukepaint::live_kind_name;
using lundukepaint::live_path_name;
using lundukepaint::marked_line_width;
using lundukepaint::recovery_file_consumed;
using lundukepaint::settle_before_open_replaces;
using lundukepaint::settle_tool_live;
using lundukepaint::shape_family_options;
using lundukepaint::stroke_after_selecting_brush_tip;
using lundukepaint::tolerance_for_click;

int g_errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_review_r4: %s\n", msg);
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
  void invalidate_canvas(lundukepaint::Rect) override {}
  void return_to_previous_tool() override {}
  Color sample_canvas(int, int) const override { return Color::white(); }
  void show_status_hint(const char*) override {}
  const lundukepaint::Pattern& active_pattern() const override {
    return lundukepaint::pattern_at(0);
  }

  int stroke_ = 1;
  int tolerance_ = 0;
  Document* document_ = nullptr;
};

int ink_on(const Document& document, int index) {
  const Layer& layer = document.layers().at(index);
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

int ink_count(const Document& document) { return ink_on(document, document.layers().active_index()); }

void paint_block(Document& document, int x, int y, int w, int h, Color color) {
  Layer& layer = document.layers().active_layer();
  for (int yy = y; yy < y + h; ++yy) {
    for (int xx = x; xx < x + w; ++xx) {
      layer.set_pixel(xx, yy, color);
    }
  }
}

struct Fix {
  std::unique_ptr<Document> doc;
  std::unique_ptr<StubHost> host;
  std::unique_ptr<Tool> tool;
};

Fix make_live(LiveKind kind) {
  Fix fix;
  fix.doc = Document::create(32, 32, Color::white());
  fix.host = std::make_unique<StubHost>(*fix.doc);
  switch (kind) {
    case LiveKind::None:
      fix.tool.reset(create_pencil_tool());
      break;
    case LiveKind::DragShape:
      fix.tool.reset(create_rectangle_tool());
      break;
    case LiveKind::PersistentShape:
      fix.tool.reset(create_polygon_tool());
      break;
    case LiveKind::TextEmpty:
    case LiveKind::TextContent:
      fix.tool.reset(create_text_tool());
      break;
    case LiveKind::Floating:
      fix.tool.reset(create_rect_select_tool());
      break;
  }
  fix.tool->set_host(fix.host.get());
  if (kind == LiveKind::DragShape) {
    fix.tool->on_press(CanvasEvent{2, 2, 1, 0});
    fix.tool->on_motion(CanvasEvent{20, 16, 0, 0});
  } else if (kind == LiveKind::PersistentShape) {
    fix.tool->on_press(CanvasEvent{2, 2, 1, 0});
    fix.tool->on_press(CanvasEvent{24, 2, 1, 0});
    fix.tool->on_press(CanvasEvent{24, 24, 1, 0});
  } else if (kind == LiveKind::TextEmpty) {
    fix.tool->on_press(CanvasEvent{4, 4, 1, 0});
  } else if (kind == LiveKind::TextContent) {
    fix.tool->on_press(CanvasEvent{4, 4, 1, 0});
    fix.tool->on_key(GDK_KEY_H, 0, "H");
  } else if (kind == LiveKind::Floating) {
    paint_block(*fix.doc, 2, 2, 4, 4, Color::black());
    fix.doc->selection().set_rect({2, 2, 4, 4});
    fix.doc->selection().lift(fix.doc->layers().active_layer(), 0);
    fix.doc->selection().move_float(12, 12);
  }
  return fix;
}

// The spec the table locks. Content is stamped, kept, or asked about.
LiveDisposition expected_disposition(LiveKind kind, LivePath path) {
  if (kind == LiveKind::None) {
    return LiveDisposition::None;
  }
  const bool empty_text = kind == LiveKind::TextEmpty;
  switch (path) {
    case LivePath::Undo:
    case LivePath::Redo:
    case LivePath::HistoryJump:
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Keep;
    case LivePath::LayerLock:
      return LiveDisposition::Leave;
    case LivePath::Recovery:
      return empty_text ? LiveDisposition::None : LiveDisposition::Composite;
    case LivePath::Quit:
    case LivePath::CloseTab:
    case LivePath::Revert:
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Prompt;
    case LivePath::ToolChange:
      if (kind == LiveKind::Floating) {
        return LiveDisposition::Leave;
      }
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Stamp;
    case LivePath::Open:
    case LivePath::NewDocument:
    case LivePath::TabSwitch:
    case LivePath::LayerChange:
    case LivePath::LayerAdd:
    case LivePath::LayerDelete:
    case LivePath::LayerMove:
    case LivePath::Save:
    case LivePath::Paste:
    case LivePath::Transform:
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Stamp;
  }
  return LiveDisposition::Stamp;
}

const LiveKind kKinds[] = {
    LiveKind::None,       LiveKind::DragShape, LiveKind::PersistentShape,
    LiveKind::TextEmpty,  LiveKind::TextContent, LiveKind::Floating,
};

const LivePath kPaths[] = {
    LivePath::ToolChange, LivePath::Undo,        LivePath::Redo,        LivePath::HistoryJump,
    LivePath::Open,       LivePath::NewDocument, LivePath::CloseTab,    LivePath::TabSwitch,
    LivePath::Quit,       LivePath::LayerChange, LivePath::LayerAdd,    LivePath::LayerDelete,
    LivePath::LayerMove,  LivePath::LayerLock,   LivePath::Save,        LivePath::Recovery,
    LivePath::Paste,      LivePath::Revert,      LivePath::Transform,
};

void test_disposition_table() {
  expect(live_edit_disposition(LiveKind::Floating, LivePath::ToolChange) == LiveDisposition::Leave,
         "a tool change keeps a floating selection");
  expect(live_edit_disposition(LiveKind::PersistentShape, LivePath::Undo) == LiveDisposition::Keep,
         "undo keeps an open shape instead of stepping history");
  expect(live_edit_disposition(LiveKind::DragShape, LivePath::Recovery) == LiveDisposition::Composite,
         "recovery composites a drag");
  expect(live_edit_disposition(LiveKind::TextEmpty, LivePath::Save) == LiveDisposition::CancelEmpty,
         "save closes an empty text box");
  expect(live_edit_disposition(LiveKind::TextContent, LivePath::Quit) == LiveDisposition::Prompt,
         "quit asks about typed text");
  expect(live_edit_disposition(LiveKind::PersistentShape, LivePath::LayerChange) ==
             LiveDisposition::Stamp,
         "a layer change stamps the shape");
  expect(live_edit_blocks_silent_close(LiveKind::Floating), "a float blocks a silent close");
  expect(!live_edit_blocks_silent_close(LiveKind::TextEmpty), "an empty text box does not");
  expect(!live_edit_blocks_silent_close(LiveKind::None), "nothing live does not block close");

  for (LiveKind kind : kKinds) {
    for (LivePath path : kPaths) {
      if (live_edit_disposition(kind, path) != expected_disposition(kind, path)) {
        std::fprintf(stderr, "test_review_r4: disposition %s x %s\n", live_kind_name(kind),
                     live_path_name(path));
        ++g_errors;
      }
    }
  }
}

bool still_live(const Fix& fix, LiveKind kind) {
  return classify_live(fix.tool.get(), fix.doc.get()) == kind;
}

void test_settle_table() {
  for (LiveKind kind : kKinds) {
    for (LivePath path : kPaths) {
      Fix fix = make_live(kind);
      if (classify_live(fix.tool.get(), fix.doc.get()) != kind) {
        std::fprintf(stderr, "test_review_r4: fixture %s did not classify\n", live_kind_name(kind));
        ++g_errors;
        continue;
      }
      const int history_before = fix.doc->history().count();
      const int ink_before = ink_on(*fix.doc, 0);
      const bool floating_before = fix.doc->selection().floating();
      const LiveDisposition disposition = expected_disposition(kind, path);
      const SettleResult result = settle_tool_live(fix.tool.get(), fix.doc.get(), path);
      char label[96];
      std::snprintf(label, sizeof(label), "%s x %s", live_kind_name(kind), live_path_name(path));
      switch (disposition) {
        case LiveDisposition::None:
        case LiveDisposition::Leave:
        case LiveDisposition::Composite:
          expect(result == SettleResult::Proceed, label);
          expect(still_live(fix, kind), label);
          expect(fix.doc->history().count() == history_before, label);
          expect(fix.doc->selection().floating() == floating_before, label);
          break;
        case LiveDisposition::Keep:
          expect(result == SettleResult::Blocked, label);
          expect(still_live(fix, kind), label);
          expect(fix.doc->history().count() == history_before, label);
          break;
        case LiveDisposition::Prompt:
          expect(result == SettleResult::Prompt, label);
          expect(still_live(fix, kind), label);
          expect(fix.doc->history().count() == history_before, label);
          break;
        case LiveDisposition::CancelEmpty:
          expect(result == SettleResult::Proceed, label);
          expect(!fix.tool->captures_keys(), label);
          expect(fix.doc->history().count() == history_before, label);
          expect(!fix.doc->unsaved_overlay(), label);
          break;
        case LiveDisposition::Stamp:
          expect(result == SettleResult::Proceed, label);
          expect(classify_live(fix.tool.get(), fix.doc.get()) == LiveKind::None, label);
          expect(!fix.doc->selection().floating(), label);
          expect(!fix.doc->unsaved_overlay(), label);
          if (kind != LiveKind::TextEmpty) {
            expect(fix.doc->history().count() > history_before || ink_on(*fix.doc, 0) > ink_before,
                   label);
          }
          break;
      }
    }
  }
}

void test_undo_does_not_drop_shape() {
  auto doc = Document::create(32, 32, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> line(create_line_tool());
  line->set_host(&host);
  line->on_press(CanvasEvent{1, 8, 1, 0});
  line->on_release(CanvasEvent{20, 8, 1, 0});
  expect(doc->history().can_undo(), "the line is a history step");
  const int ink = ink_count(*doc);
  std::unique_ptr<Tool> polygon(create_polygon_tool());
  polygon->set_host(&host);
  polygon->on_press(CanvasEvent{2, 2, 1, 0});
  polygon->on_press(CanvasEvent{18, 2, 1, 0});
  polygon->on_press(CanvasEvent{18, 18, 1, 0});
  doc->set_on_disrupt([&](const char* action) {
    LivePath path = LivePath::LayerChange;
    if (action != nullptr && std::strcmp(action, "undo") == 0) {
      path = LivePath::Undo;
    } else if (action != nullptr && std::strcmp(action, "redo") == 0) {
      path = LivePath::Redo;
    } else if (action != nullptr && std::strcmp(action, "history-jump") == 0) {
      path = LivePath::HistoryJump;
    }
    return settle_tool_live(polygon.get(), doc.get(), path) != SettleResult::Blocked;
  });
  doc->undo();
  expect(polygon->has_uncommitted_preview(), "undo left the polygon up");
  expect(ink_count(*doc) == ink, "undo did not also remove the previous line");
  expect(doc->history().can_undo(), "the line is still undoable");
  doc->redo();
  expect(polygon->has_uncommitted_preview(), "redo left the polygon up");
  doc->jump_history(-1);
  expect(polygon->has_uncommitted_preview(), "a history jump left the polygon up");
}

void test_layer_change_stamps_source() {
  auto doc = Document::create(32, 32, Color::white());
  expect(doc->add_layer(), "second layer");
  expect(doc->set_active_layer(0), "back to the first layer");
  StubHost host(*doc);
  std::unique_ptr<Tool> polygon(create_polygon_tool());
  polygon->set_host(&host);
  polygon->on_press(CanvasEvent{2, 2, 1, 0});
  polygon->on_press(CanvasEvent{22, 4, 1, 0});
  polygon->on_press(CanvasEvent{8, 22, 1, 0});
  expect(polygon->preview_layer() == 0, "the polygon was armed on layer 0");
  doc->set_on_disrupt([&](const char*) {
    return settle_tool_live(polygon.get(), doc.get(), LivePath::LayerChange) !=
           SettleResult::Blocked;
  });
  expect(doc->set_active_layer(1), "layer change proceeds after the stamp");
  expect(!polygon->has_uncommitted_preview(), "the polygon is no longer live");
  auto black_on = [](const Document& document, int index) {
    const Layer& layer = document.layers().at(index);
    int n = 0;
    for (int y = 0; y < layer.height(); ++y) {
      for (int x = 0; x < layer.width(); ++x) {
        if (layer.pixel(x, y) == Color::black()) {
          ++n;
        }
      }
    }
    return n;
  };
  expect(black_on(*doc, 0) > 0, "the polygon landed on the layer it was started on");
  expect(black_on(*doc, 1) == 0, "the polygon did not land on the layer switched to");
  expect(doc->layers().active_index() == 1, "the active layer did change");
}

void test_locked_finish_keeps_preview() {
  auto doc = Document::create(32, 32, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> polygon(create_polygon_tool());
  polygon->set_host(&host);
  polygon->on_press(CanvasEvent{2, 2, 1, 0});
  polygon->on_press(CanvasEvent{20, 2, 1, 0});
  polygon->on_press(CanvasEvent{20, 20, 1, 0});
  doc->set_on_disrupt([&](const char*) { return true; });
  doc->set_layer_locked(0, true);
  expect(polygon->has_uncommitted_preview(), "locking does not drop the polygon");
  expect(!polygon->on_commit(), "Enter on a locked layer refuses");
  expect(polygon->has_uncommitted_preview(), "the preview stays up");
  expect(!doc->history().can_undo() || doc->history().count() == 1,
         "the refused shape is not a pixel undo");
  // The lock itself may be a history step. The shape pixels must not be.
  expect(ink_count(*doc) == 0, "no shape pixels were written while locked");
  doc->set_layer_locked(0, false);
  expect(polygon->on_commit(), "unlocking lets Enter place the shape");
  expect(ink_count(*doc) > 0, "the shape lands after unlock");
}

void test_locked_text_blocks_switch() {
  auto doc = Document::create(80, 40, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> text(create_text_tool());
  text->set_host(&host);
  text->on_press(CanvasEvent{4, 4, 1, 0});
  expect(text->on_key(GDK_KEY_H, 0, "H"), "typing opens content");
  doc->set_layer_locked(0, true);
  expect(settle_tool_live(text.get(), doc.get(), LivePath::ToolChange) == SettleResult::Blocked,
         "a locked layer blocks the tool change");
  expect(text->captures_keys(), "the text box stays on this document");
  expect(settle_tool_live(text.get(), doc.get(), LivePath::TabSwitch) == SettleResult::Blocked,
         "a locked layer blocks the tab switch");
  expect(text->captures_keys() && doc->unsaved_overlay(), "the words stay on this tab");
  doc->set_layer_locked(0, false);
  expect(settle_tool_live(text.get(), doc.get(), LivePath::TabSwitch) == SettleResult::Proceed,
         "an unlocked layer stamps before the tab switch");
  expect(!text->captures_keys(), "a successful stamp closes the box first");
  expect(doc->history().can_undo(), "the words are a history entry on this document");
}

void test_tool_change_keeps_float() {
  Fix fix = make_live(LiveKind::Floating);
  const int history = fix.doc->history().count();
  expect(settle_tool_live(fix.tool.get(), fix.doc.get(), LivePath::ToolChange) ==
             SettleResult::Proceed,
         "switching tools is allowed");
  expect(fix.doc->selection().floating(), "the paste stays floating");
  expect(fix.doc->selection().float_x() == 12 && fix.doc->selection().float_y() == 12,
         "the float stays where it was");
  expect(fix.doc->history().count() == history, "the tool change did not commit the float");
}

void test_pasted_text() {
  auto doc = Document::create(80, 40, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> text(create_text_tool());
  text->set_host(&host);
  text->on_press(CanvasEvent{4, 4, 1, 0});
  expect(!doc->unsaved_overlay(), "an empty box is not unsaved");
  expect(text->paste_text("Yo"), "paste inserts into the box");
  expect(text->has_uncommitted_preview(), "pasted text is uncommitted content");
  expect(doc->unsaved_overlay() && doc->dirty(), "pasted text dirties the document");
  expect(settle_tool_live(text.get(), doc.get(), LivePath::Save) == SettleResult::Proceed,
         "save stamps pasted text");
  expect(!text->captures_keys(), "the box closes after the stamp");
  expect(doc->history().can_undo(), "pasted text is one undo step");
}

void test_recovery_composites_without_commit() {
  Fix shape = make_live(LiveKind::PersistentShape);
  const int history = shape.doc->history().count();
  std::vector<std::uint8_t> copy(static_cast<std::size_t>(shape.doc->width()) *
                                     static_cast<std::size_t>(shape.doc->height()) * 4,
                                 255);
  expect(shape.tool->paint_recovery_overlay(0, copy.data(), shape.doc->width(), shape.doc->height(),
                                            shape.doc->width() * 4),
         "recovery paints the open polygon");
  bool ink = false;
  for (std::uint8_t byte : copy) {
    if (byte != 255) {
      ink = true;
      break;
    }
  }
  expect(ink, "the recovery image includes the polygon");
  expect(shape.tool->has_uncommitted_preview(), "recovery did not finish the polygon");
  expect(shape.doc->history().count() == history, "recovery did not commit");

  Fix floated = make_live(LiveKind::Floating);
  const int float_history = floated.doc->history().count();
  std::vector<std::uint8_t> layer_copy(
      static_cast<std::size_t>(floated.doc->layers().active_layer().stride()) *
          static_cast<std::size_t>(floated.doc->height()));
  const Layer& src = floated.doc->layers().active_layer();
  std::memcpy(layer_copy.data(), src.pixels(), layer_copy.size());
  expect(composite_floating_into_buffer(floated.doc->selection(), layer_copy.data(), src.width(),
                                       src.height(), src.stride(), src.offset_x(), src.offset_y()),
         "recovery paints the float");
  const std::uint8_t* moved =
      layer_copy.data() + (static_cast<std::size_t>(12) * static_cast<std::size_t>(src.stride()) +
                           static_cast<std::size_t>(12) * 4);
  expect(moved[0] == 0 && moved[3] == 255, "the float's black pixel is in the snapshot");
  expect(src.pixel(12, 12) == Color::white(), "the live layer was not touched");
  expect(floated.doc->selection().floating(), "the float is still live");
  expect(floated.doc->history().count() == float_history, "float recovery did not commit");
}

void test_open_keeps_stamp_off_placeholder() {
  expect(!settle_before_open_replaces(false), "a failed open does not replace");
  expect(settle_before_open_replaces(true), "only a loaded replace may discard");
  expect(!recovery_file_consumed(false), "a failed recover keeps the file");
  expect(recovery_file_consumed(true), "a successful recover may unlink");

  Workspace workspace;
  workspace.add(Document::create(32, 32, Color::white()));
  expect(workspace.is_placeholder(0), "a blank tab is a placeholder");
  Document& doc = workspace.active();
  StubHost host(doc);
  std::unique_ptr<Tool> polygon(create_polygon_tool());
  polygon->set_host(&host);
  polygon->on_press(CanvasEvent{2, 2, 1, 0});
  polygon->on_press(CanvasEvent{16, 2, 1, 0});
  polygon->on_press(CanvasEvent{16, 16, 1, 0});
  expect(settle_tool_live(polygon.get(), &doc, LivePath::Open) == SettleResult::Proceed,
         "open stamps the polygon first");
  expect(ink_on(doc, 0) > 0, "the stamp is on the original tab");
  expect(!workspace.is_placeholder(0), "the stamped tab is no longer a placeholder");
}

void test_delete_restores_selection() {
  auto doc = Document::create(16, 16, Color::white());
  paint_block(*doc, 2, 2, 4, 4, Color::black());
  doc->selection().set_rect({2, 2, 4, 4});
  doc->delete_selection();
  expect(!doc->selection().empty(), "a non-floating delete keeps the ants");
  expect(doc->layers().active_layer().pixel(3, 3).a == 0, "the pixels were cleared");
  doc->undo();
  expect(doc->layers().active_layer().pixel(3, 3) == Color::black(), "undo restores the pixels");
  expect(!doc->selection().empty() && doc->selection().bounds().w == 4,
         "undo restores the ants");
  doc->redo();
  expect(!doc->selection().empty(), "redo keeps the ants");

  auto floated = Document::create(16, 16, Color::white());
  paint_block(*floated, 1, 1, 3, 3, Color::black());
  floated->selection().set_rect({1, 1, 3, 3});
  expect(floated->selection().lift(floated->layers().active_layer(), 0), "lift");
  floated->selection().move_float(6, 6);
  floated->delete_selection();
  expect(!floated->selection().floating() && floated->selection().empty(),
         "deleting a float clears it");
  floated->undo();
  expect(floated->selection().floating(), "undo puts the float back");
  expect(floated->selection().float_x() == 6 && floated->selection().float_y() == 6,
         "undo puts the float back where it was");
}

void draw_ellipse(Tool& tool) {
  tool.on_press(CanvasEvent{4, 4, 1, 0});
  tool.on_motion(CanvasEvent{26, 22, 0, 0});
  tool.on_release(CanvasEvent{26, 22, 1, 0});
}

void draw_round(Tool& tool) {
  tool.on_press(CanvasEvent{2, 2, 1, 0});
  tool.on_motion(CanvasEvent{28, 28, 0, 0});
  tool.on_release(CanvasEvent{28, 28, 1, 0});
}

int ellipse_ink(bool antialias, bool filled) {
  shape_family_options("ellipse").antialias = antialias;
  auto doc = Document::create(32, 32, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> tool(filled ? create_ellipse_fill_tool() : create_ellipse_tool());
  tool->set_host(&host);
  draw_ellipse(*tool);
  return ink_count(*doc);
}

int round_ink(int radius, bool filled) {
  shape_family_options("rounded-rect").antialias = false;
  shape_family_options("rounded-rect").corner_radius = radius;
  auto doc = Document::create(32, 32, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> tool(filled ? create_rounded_rect_fill_tool() : create_rounded_rect_tool());
  tool->set_host(&host);
  draw_round(*tool);
  return ink_count(*doc);
}

void test_shape_options_follow_family() {
  const int hard = ellipse_ink(false, false);
  const int soft = ellipse_ink(true, false);
  expect(hard > 0 && soft > hard, "ellipse antialias follows the shared family flag");
  expect(ellipse_ink(true, false) == soft, "a second outline tool reads the same flag");
  expect(round_ink(1, false) != round_ink(16, false), "outline corner radius follows the family");
  expect(round_ink(1, true) != round_ink(16, true), "filled corner radius follows the family");
  expect(round_ink(16, true) == round_ink(16, true), "two filled tools share the radius");
}

void test_transparent_move_is_shared() {
  auto doc = Document::create(32, 32, Color::white());
  StubHost host(*doc);
  doc->selection().set_transparent_move(true);
  std::unique_ptr<Tool> rect(create_rect_select_tool());
  std::unique_ptr<Tool> lasso(create_lasso_tool());
  std::unique_ptr<Tool> ellipse(create_ellipse_select_tool());
  rect->set_host(&host);
  lasso->set_host(&host);
  ellipse->set_host(&host);
  rect->on_press(CanvasEvent{1, 1, 1, 0});
  rect->on_release(CanvasEvent{8, 8, 1, 0});
  expect(doc->selection().transparent_move(), "rect select does not clear transparent move");
  lasso->on_press(CanvasEvent{2, 2, 1, 0});
  lasso->on_release(CanvasEvent{2, 2, 1, 0});
  expect(doc->selection().transparent_move(), "lasso does not clear transparent move");
  ellipse->on_press(CanvasEvent{3, 3, 1, 0});
  ellipse->on_release(CanvasEvent{10, 10, 1, 0});
  expect(doc->selection().transparent_move(), "ellipse select does not clear transparent move");
}

void test_brush_and_tolerance() {
  const int choices[] = {1, 2, 3, 5, 8};
  expect(stroke_after_selecting_brush_tip(4, 16) == 4, "a brush tip does not rewrite stroke width");
  expect(marked_line_width(4, choices, 5) == -1, "width 4 checks no row");
  expect(marked_line_width(5, choices, 5) == 5, "width 5 checks the 5px row");
  expect(marked_line_width(8, choices, 5) == 8, "width 8 checks the 8px row");
  expect(tolerance_for_click(12, 200) == 12, "the tool tolerance wins over the host");
  expect(tolerance_for_click(-3, 9) == 0, "a negative tolerance clamps to exact");

  auto doc = Document::create(16, 16, Color::white());
  StubHost host(*doc);
  host.set_fill_tolerance(255);
  paint_block(*doc, 4, 4, 1, 1, Color::black());
  doc->layers().active_layer().set_pixel(5, 4, Color{40, 40, 40, 255});
  std::unique_ptr<Tool> fill(create_fill_tool());
  fill->set_host(&host);
  fill->on_press(CanvasEvent{4, 4, 1, 0});
  expect(doc->layers().active_layer().pixel(5, 4) == Color{40, 40, 40, 255},
         "fill runs tolerance 0, not the host's 255");

  auto wand_doc = Document::create(16, 16, Color::white());
  StubHost wand_host(*wand_doc);
  wand_host.set_fill_tolerance(255);
  paint_block(*wand_doc, 4, 4, 1, 1, Color::black());
  wand_doc->layers().active_layer().set_pixel(5, 4, Color{40, 40, 40, 255});
  std::unique_ptr<Tool> wand(create_magic_wand_tool());
  wand->set_host(&wand_host);
  wand->on_press(CanvasEvent{4, 4, 1, 0});
  expect(!wand_doc->selection().empty(), "the wand selected the clicked pixel");
  expect(wand_doc->selection().bounds().w == 1 && wand_doc->selection().bounds().h == 1,
         "the wand used tolerance 0 and did not include the neighbour");
}

Gtk::CheckButton* find_check(Gtk::Widget* widget) {
  auto* box = dynamic_cast<Gtk::Box*>(widget);
  if (box == nullptr) {
    return nullptr;
  }
  for (Gtk::Widget* child : box->get_children()) {
    if (auto* check = dynamic_cast<Gtk::CheckButton*>(child)) {
      return check;
    }
  }
  return nullptr;
}

Gtk::SpinButton* find_spin(Gtk::Widget* widget) {
  auto* box = dynamic_cast<Gtk::Box*>(widget);
  if (box == nullptr) {
    return nullptr;
  }
  for (Gtk::Widget* child : box->get_children()) {
    if (auto* spin = dynamic_cast<Gtk::SpinButton*>(child)) {
      return spin;
    }
  }
  return nullptr;
}

void test_option_widgets() {
  int argc = 0;
  char** argv = nullptr;
  if (!gtk_init_check(&argc, &argv)) {
    std::printf("test_review_r4: no display, option widgets not constructed\n");
    return;
  }
  Gtk::Main::init_gtkmm_internals();

  shape_family_options("ellipse").antialias = true;
  auto doc = Document::create(32, 32, Color::white());
  StubHost host(*doc);
  std::unique_ptr<Tool> outline(create_ellipse_tool());
  std::unique_ptr<Tool> filled(create_ellipse_fill_tool());
  outline->set_host(&host);
  filled->set_host(&host);
  Gtk::CheckButton* outline_aa = find_check(outline->options_widget());
  expect(outline_aa != nullptr && outline_aa->get_active(), "outline shows the shared antialias");
  if (outline_aa != nullptr) {
    outline_aa->set_active(false);
  }
  Gtk::CheckButton* filled_aa = find_check(filled->options_widget());
  expect(filled_aa != nullptr && !filled_aa->get_active(),
         "the filled ellipse shows the antialias the outline just set");

  shape_family_options("rounded-rect").corner_radius = 9;
  std::unique_ptr<Tool> round_outline(create_rounded_rect_tool());
  std::unique_ptr<Tool> round_filled(create_rounded_rect_fill_tool());
  round_outline->set_host(&host);
  round_filled->set_host(&host);
  Gtk::SpinButton* outline_radius = find_spin(round_outline->options_widget());
  expect(outline_radius != nullptr && outline_radius->get_value_as_int() == 9,
         "outline shows the shared corner radius");
  if (outline_radius != nullptr) {
    outline_radius->set_value(21);
  }
  Gtk::SpinButton* filled_radius = find_spin(round_filled->options_widget());
  expect(filled_radius != nullptr && filled_radius->get_value_as_int() == 21,
         "the filled rounded rect shows the radius the outline just set");

  doc->selection().set_transparent_move(true);
  std::unique_ptr<Tool> rect(create_rect_select_tool());
  std::unique_ptr<Tool> lasso(create_lasso_tool());
  rect->set_host(&host);
  lasso->set_host(&host);
  Gtk::CheckButton* rect_check = find_check(rect->options_widget());
  expect(rect_check != nullptr && rect_check->get_active(),
         "rect select shows the document transparent-move flag");
  if (rect_check != nullptr) {
    rect_check->set_active(false);
  }
  Gtk::CheckButton* lasso_check = find_check(lasso->options_widget());
  expect(lasso_check != nullptr && !lasso_check->get_active(),
         "lasso shows the same transparent-move flag");
  expect(!doc->selection().transparent_move(), "the checkbox writes the one shared flag");

  std::unique_ptr<Tool> fill(create_fill_tool());
  std::unique_ptr<Tool> wand(create_magic_wand_tool());
  fill->set_host(&host);
  wand->set_host(&host);
  host.set_fill_tolerance(255);
  Gtk::SpinButton* fill_spin = find_spin(fill->options_widget());
  Gtk::SpinButton* wand_spin = find_spin(wand->options_widget());
  expect(fill_spin != nullptr && wand_spin != nullptr, "similarity spins exist");
  if (fill_spin != nullptr && wand_spin != nullptr) {
    fill_spin->set_value(0);
    wand_spin->set_value(80);
    auto clicked = Document::create(16, 16, Color::white());
    StubHost click_host(*clicked);
    click_host.set_fill_tolerance(255);
    paint_block(*clicked, 4, 4, 1, 1, Color::black());
    clicked->layers().active_layer().set_pixel(5, 4, Color{40, 40, 40, 255});
    fill->set_host(&click_host);
    fill->on_press(CanvasEvent{4, 4, 1, 0});
    expect(clicked->layers().active_layer().pixel(5, 4) == Color{40, 40, 40, 255},
           "the fill spin's 0 is the tolerance that runs");

    auto wide = Document::create(16, 16, Color::white());
    StubHost wide_host(*wide);
    wide_host.set_fill_tolerance(0);
    paint_block(*wide, 4, 4, 1, 1, Color::black());
    wide->layers().active_layer().set_pixel(5, 4, Color{40, 40, 40, 255});
    wand->set_host(&wide_host);
    wand->on_press(CanvasEvent{4, 4, 1, 0});
    expect(wide->selection().bounds().w >= 2, "the wand spin's 80 is the tolerance that runs");
  }
}

}  // namespace

int main() {
  test_disposition_table();
  test_settle_table();
  test_undo_does_not_drop_shape();
  test_layer_change_stamps_source();
  test_locked_finish_keeps_preview();
  test_locked_text_blocks_switch();
  test_tool_change_keeps_float();
  test_pasted_text();
  test_recovery_composites_without_commit();
  test_open_keeps_stamp_off_placeholder();
  test_delete_restores_selection();
  test_shape_options_follow_family();
  test_transparent_move_is_shared();
  test_brush_and_tolerance();
  test_option_widgets();
  if (g_errors != 0) {
    std::fprintf(stderr, "test_review_r4: %d failure(s)\n", g_errors);
    return 1;
  }
  std::printf("test_review_r4: ok\n");
  return 0;
}
