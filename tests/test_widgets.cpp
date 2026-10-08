// SPDX-License-Identifier: GPL-3.0-or-later
//
// Widget-level checks that need a real X display. A normal `meson test` with
// no display exits 77 (meson "SKIP"). packaging/build-deb.sh sets
// LUNDUKEPAINT_REQUIRE_DISPLAY=1 and starts Xvfb itself; under that variable a
// missing display is a failure, not a skip. The checks do not need a window
// manager or keyboard focus. They exercise:
//   * the toolbox selected-tool highlight moving between buttons,
//   * the canvas view never inflating the toplevel window when a big image is
//     loaded (the "opening an image balloons the window" bug),
//   * canvas_to_screen() staying self-consistent with the centred canvas
//     (the text tool popup position),
//   * the startup greeting API (start/skip/cancel) when exercised directly —
//     empty Application launch does not require howdy (gated off by default).

#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "tools/rail_options.hpp"
#include "ui/canvas_view.hpp"
#include "ui/intro_howdy.hpp"
#include "ui/layers_panel.hpp"
#include "ui/status_bar.hpp"
#include "ui/toolbox.hpp"
#include "ui/toolbox_catalog.hpp"

#include <cstdlib>
#include <cstring>

#include <glib.h>
#include <gtk/gtk.h>
#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/main.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>

#include "tools/tool.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

using lundukepaint::CanvasView;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Layer;
using lundukepaint::LayersPanel;
using lundukepaint::StatusBar;
using lundukepaint::Toolbox;
using lundukepaint::configure_rail_check;
using lundukepaint::configure_rail_combo;
using lundukepaint::prepare_rail_box;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_widgets: %s\n", msg);
    ++errors;
  }
}

void expect_covers_request(Gtk::Widget& widget, const char* msg) {
  int min_w = 0;
  int nat_w = 0;
  int min_h = 0;
  int nat_h = 0;
  widget.get_preferred_width(min_w, nat_w);
  widget.get_preferred_height(min_h, nat_h);
  const int aw = widget.get_allocated_width();
  const int ah = widget.get_allocated_height();
  if (aw < min_w || ah < min_h || aw < 1 || ah < 1) {
    std::fprintf(stderr, "test_widgets: %s (allocated %dx%d, minimum %dx%d)\n", msg, aw, ah, min_w,
                 min_h);
    ++errors;
  }
}

class IdTool : public lundukepaint::Tool {
public:
  explicit IdTool(const char* id) : id_(id) {}
  const char* id() const override { return id_; }
  const char* name() const override { return id_; }
  char shortcut() const override { return 0; }
  void on_press(lundukepaint::CanvasEvent) override {}
  void on_motion(lundukepaint::CanvasEvent) override {}
  void on_release(lundukepaint::CanvasEvent) override {}
  void on_cancel() override {}

private:
  const char* id_;
};

// Classic outer width from acd6af9, still the rail in tags v0.9-2 through v0.9-5.
constexpr int kClassicRailWidth = 82;

bool widget_past_rail(Gtk::Widget& rail, Gtk::Widget& child, int& x, int& y) {
  x = 0;
  y = 0;
  if (!child.get_mapped() || !child.get_visible()) {
    return false;
  }
  if (!child.translate_coordinates(rail, 0, 0, x, y)) {
    return false;
  }
  const int cw = child.get_allocated_width();
  const int ch = child.get_allocated_height();
  if (cw < 1 || ch < 1) {
    return false;
  }
  const int rw = rail.get_allocated_width();
  const int rh = rail.get_allocated_height();
  return x < -1 || y < -1 || x + cw > rw + 1 || y + ch > rh + 1;
}

void expect_rail_contains(Gtk::Widget& rail, Gtk::Widget& child) {
  int x = 0;
  int y = 0;
  if (!widget_past_rail(rail, child, x, y)) {
    auto* container = dynamic_cast<Gtk::Container*>(&child);
    if (container == nullptr) {
      return;
    }
    for (Gtk::Widget* next : container->get_children()) {
      if (next != nullptr) {
        expect_rail_contains(rail, *next);
      }
    }
    return;
  }
  const char* type = G_OBJECT_TYPE_NAME(child.gobj());
  const Glib::ustring tip = child.get_tooltip_text();
  std::fprintf(stderr,
               "test_widgets: %s \"%s\" extends past the rail at %d,%d size %dx%d (rail %dx%d)\n",
               type, tip.c_str(), x, y, child.get_allocated_width(), child.get_allocated_height(),
               rail.get_allocated_width(), rail.get_allocated_height());
  ++errors;
}

void expect_picker_naturals(Gtk::Widget& rail, Gtk::Widget& widget) {
  const Glib::ustring tip = widget.get_tooltip_text();
  if (tip == "Line width" || tip == "Brush shape" || tip == "Spray radius") {
    int min_w = 0;
    int nat_w = 0;
    widget.get_preferred_width(min_w, nat_w);
    const int rail_w = rail.get_allocated_width();
    if (nat_w < 1 || nat_w > rail_w) {
      std::fprintf(stderr, "test_widgets: picker \"%s\" natural width %d, rail %d\n", tip.c_str(),
                   nat_w, rail_w);
      ++errors;
    }
  }
  auto* container = dynamic_cast<Gtk::Container*>(&widget);
  if (container == nullptr) {
    return;
  }
  for (Gtk::Widget* child : container->get_children()) {
    if (child != nullptr) {
      expect_picker_naturals(rail, *child);
    }
  }
}

void pump(int ms) {
  const gint64 end = g_get_monotonic_time() + static_cast<gint64>(ms) * 1000;
  do {
    while (gtk_events_pending()) {
      gtk_main_iteration_do(FALSE);
    }
    g_usleep(2000);
  } while (g_get_monotonic_time() < end);
}

std::vector<std::uint8_t> dump(const Layer& layer) {
  std::vector<std::uint8_t> out(static_cast<std::size_t>(layer.stride()) *
                                static_cast<std::size_t>(layer.height()));
  for (std::size_t i = 0; i < out.size(); ++i) {
    out[i] = layer.pixels()[i];
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_widgets: display required (LUNDUKEPAINT_REQUIRE_DISPLAY) but gtk_init_check failed\n");
      return 1;
    }
    std::printf("test_widgets: no display, skipping\n");
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();

  // --- Toolbox: the highlight follows the active tool ---------------------
  {
    Gtk::Window window;
    Toolbox toolbox;
    toolbox.add_tool_button("pencil", "Pencil (P)", "tool-pencil-symbolic");
    toolbox.add_tool_button("brush", "Brush (B)", "tool-paintbrush-symbolic");
    toolbox.add_tool_button("text", "Text (T)", "tool-text-symbolic");
    window.add(toolbox);
    window.show_all();
    pump(120);

    toolbox.set_active_tool("pencil");
    expect(toolbox.active_tool_id() == "pencil", "active id is pencil");
    expect(toolbox.tool_button_selected("pencil"), "pencil button is highlighted");
    expect(!toolbox.tool_button_selected("brush"), "brush button is not highlighted");

    toolbox.set_active_tool("text");
    expect(toolbox.active_tool_id() == "text", "active id moved to text");
    expect(toolbox.tool_button_selected("text"), "text button is highlighted");
    expect(!toolbox.tool_button_selected("pencil"), "pencil highlight cleared");

    toolbox.set_active_tool("no-such-tool");
    expect(toolbox.tool_button_selected("text"), "an unknown id keeps the current highlight");

    expect(toolbox.tool_columns_homogeneous(), "tool grid columns are homogeneous");
    expect(toolbox.tool_columns_equal_width(), "the two tool columns have equal width");
    expect(toolbox.width_tracks_tool_grid(),
           "toolbox width hugs tool grid (allocated <= grid + ~36px of pad/air)");
    window.hide();
    pump(50);
  }

  // --- CanvasView: a big image must not inflate the window ---------------
  {
    Gtk::Window window;
    window.set_default_size(1100, 720);
    CanvasView canvas;
    window.add(canvas);
    window.show_all();
    pump(300);

    int before_w = window.get_allocated_width();
    int before_h = window.get_allocated_height();
    expect(before_w > 0 && before_h > 0, "window got an allocation");

    int min_w = 0;
    int nat_w = 0;
    int min_h = 0;
    int nat_h = 0;
    canvas.get_preferred_width(min_w, nat_w);
    canvas.get_preferred_height(min_h, nat_h);
    expect(min_w <= 400 && nat_w <= 400, "empty canvas request is bounded");

    auto big = Document::create(4000, 3000, Color::white(), "Background");
    canvas.set_document(big.get());
    canvas.refresh_size();
    pump(400);

    canvas.get_preferred_width(min_w, nat_w);
    canvas.get_preferred_height(min_h, nat_h);
    std::printf("test_widgets: 4000x3000 canvas request min=%dx%d nat=%dx%d, window %dx%d\n",
                min_w, min_h, nat_w, nat_h, window.get_allocated_width(),
                window.get_allocated_height());
    expect(min_w <= 400, "min width stays bounded with a 4000 px image");
    expect(min_h <= 400, "min height stays bounded with a 3000 px image");
    expect(nat_w <= 400, "natural width stays bounded with a 4000 px image");
    expect(nat_h <= 400, "natural height stays bounded with a 3000 px image");
    expect(window.get_allocated_width() <= before_w + 8,
           "window did not grow horizontally when the image was loaded");
    expect(window.get_allocated_height() <= before_h + 8,
           "window did not grow vertically when the image was loaded");

    // --- canvas_to_screen: consistent with the centred canvas ------------
    int x0 = 0;
    int y0 = 0;
    int x10 = 0;
    int y10 = 0;
    const bool ok0 = canvas.canvas_to_screen(0, 0, x0, y0);
    const bool ok10 = canvas.canvas_to_screen(10, 10, x10, y10);
    expect(ok0 && ok10, "canvas_to_screen resolved a screen position");
    if (ok0 && ok10) {
      expect(x10 - x0 == 10 && y10 - y0 == 10, "10 canvas px is 10 screen px at 100%");
      // Xvfb with no window manager may report an origin at (0, 0) or place
      // the frame a few pixels off the output. The translation only has to
      // stay near the window; it does not have to be focused or reparented.
      if (auto win = window.get_window()) {
        int win_x = 0;
        int win_y = 0;
        win->get_origin(win_x, win_y);
        const int rel_x = x0 - win_x;
        const int rel_y = y0 - win_y;
        const int ww = std::max(1, window.get_allocated_width());
        const int wh = std::max(1, window.get_allocated_height());
        expect(rel_x > -ww && rel_y > -wh && rel_x < ww * 2 && rel_y < wh * 2,
               "canvas origin is near the window without a window manager");
      }
    }
    canvas.set_document(nullptr);
    window.hide();
    pump(50);
  }

  // --- Startup greeting: one second, skippable, never dirties ------------
  {
    Gtk::Window window;
    window.set_default_size(900, 600);
    CanvasView canvas;
    window.add(canvas);
    window.show_all();
    pump(200);

    auto doc = Document::create(640, 480, Color::white(), "Background");
    doc->mark_clean();
    canvas.set_document(doc.get());
    pump(100);
    const std::vector<std::uint8_t> pristine = dump(doc->layers().active_layer());

    canvas.start_intro();
    expect(canvas.intro_active(), "greeting started");
    expect(canvas.intro_visible(), "greeting is visible while it plays");
    pump(300);
    expect(canvas.intro_active(), "greeting still running after 300 ms");
    expect(!doc->dirty(), "greeting does not dirty the document");
    expect(dump(doc->layers().active_layer()) == pristine,
           "greeting does not paint into the layer");
    pump(900);
    expect(!canvas.intro_active(), "greeting animation finished on its own after ~1 s");
    expect(canvas.intro_visible(), "finished howdy stays on the canvas");
    expect(!doc->dirty(), "document still clean when the greeting ends");
    expect(doc->history().count() == 0, "greeting pushed no history");
    expect(dump(doc->layers().active_layer()) == pristine,
           "layer pixels untouched once the greeting ended");

    // Skip after complete must leave the finished word.
    canvas.skip_intro();
    expect(canvas.intro_visible(), "skip after complete leaves the finished howdy");
    expect(!doc->dirty(), "skip after complete still does not dirty");

    // Paint-over / explicit cancel clears a finished overlay.
    canvas.cancel_intro();
    expect(!canvas.intro_visible(), "cancel after complete clears the overlay");

    // Skip mid-animation dismisses (does not hold a partial stroke).
    canvas.start_intro();
    expect(canvas.intro_active(), "greeting restarted for the skip check");
    pump(80);
    canvas.skip_intro();
    expect(!canvas.intro_active(), "skip stops the greeting immediately");
    expect(!canvas.intro_visible(), "skip during animation clears the intro");
    expect(!doc->dirty(), "skip leaves the document clean");

    // Replacing the document also stops it.
    canvas.start_intro();
    canvas.set_document(nullptr);
    expect(!canvas.intro_active(), "loading another document stops the greeting");
    expect(!canvas.intro_visible(), "loading another document clears the overlay");
    window.hide();
    pump(50);
  }

  // Finding 8: a timed status message is visible and a new hint does not
  // replace it until the timer ends.
  {
    Gtk::Window window;
    StatusBar bar;
    window.add(bar);
    window.show_all();
    pump(40);
    bar.set_hint("Pencil");
    bar.show_message("Grid on");
    auto* hint = dynamic_cast<Gtk::Label*>(bar.get_children().front());
    expect(hint != nullptr && hint->get_text() == "Grid on", "status message is on the bar");
    bar.set_hint("Line");
    expect(hint != nullptr && hint->get_text() == "Grid on", "hint waits while a message is showing");
    window.hide();
    pump(30);
  }

  // Finding 9: tool options replace the empty rail page and stay inside the
  // toolbox width at the default window size.
  {
    Gtk::Window window;
    window.set_default_size(1100, 720);
    Toolbox toolbox;
    toolbox.add_tool_button("pencil", "Pencil", "tool-pencil-symbolic");
    toolbox.add_tool_button("line", "Line", "tool-line-symbolic");
    toolbox.add_tool_button("text", "Text", "tool-text-symbolic");
    toolbox.add_tool_button("fill", "Fill", "tool-fill-symbolic");
    auto* options = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
    prepare_rail_box(*options);
    auto* font = Gtk::manage(new Gtk::ComboBoxText());
    font->append("Liberation Sans");
    font->set_active(0);
    configure_rail_combo(*font);
    auto* bold = Gtk::manage(new Gtk::CheckButton("Bold"));
    configure_rail_check(*bold);
    auto* italic = Gtk::manage(new Gtk::CheckButton("Italic"));
    configure_rail_check(*italic);
    auto* size = Gtk::manage(new Gtk::SpinButton());
    size->set_range(1, 256);
    size->set_increments(1, 10);
    size->set_value(16);
    lundukepaint::configure_rail_spin(*size);
    auto* similar = Gtk::manage(new Gtk::Label("Similarity"));
    auto* move = Gtk::manage(new Gtk::CheckButton("Transparent move"));
    configure_rail_check(*move);
    auto* aa = Gtk::manage(new Gtk::CheckButton("Anti-alias"));
    configure_rail_check(*aa);
    options->pack_start(*font, Gtk::PACK_SHRINK);
    options->pack_start(*size, Gtk::PACK_SHRINK);
    options->pack_start(*bold, Gtk::PACK_SHRINK);
    options->pack_start(*italic, Gtk::PACK_SHRINK);
    options->pack_start(*similar, Gtk::PACK_SHRINK);
    options->pack_start(*move, Gtk::PACK_SHRINK);
    options->pack_start(*aa, Gtk::PACK_SHRINK);
    window.add(toolbox);
    window.show_all();
    pump(80);
    toolbox.set_active_tool("text");
    toolbox.set_tool_options(options);
    pump(80);
    expect(bold->get_visible() && bold->get_mapped(), "text options are shown in the rail");
    expect(toolbox.width_tracks_tool_grid(), "options rail still fits the tool grid");
    expect(window.get_allocated_width() <= 1100 + 40, "default window does not grow for options");
    expect(std::abs(toolbox.get_allocated_width() - 82) <= 1,
           "rail stays at the classic 82px width");
    expect_covers_request(*font, "font combo at the default window");
    expect_covers_request(*size, "size spin at the default window");
    expect_covers_request(*bold, "Bold at the default window");
    expect_covers_request(*italic, "Italic at the default window");
    expect_covers_request(*move, "Transparent move at the default window");
    window.resize(1100, 240);
    pump(120);
    expect(std::abs(toolbox.get_allocated_width() - 82) <= 1,
           "rail keeps the classic width in a short window");
    expect_covers_request(*font, "font combo in a short window");
    expect_covers_request(*size, "size spin in a short window");
    expect_covers_request(*bold, "Bold in a short window");
    expect_covers_request(*italic, "Italic in a short window");
    expect_covers_request(*move, "Transparent move in a short window");
    toolbox.set_active_tool("line");
    pump(40);
    expect(toolbox.width_tracks_tool_grid(), "line width plus antialias still fits");
    toolbox.set_tool_options(nullptr);
    toolbox.set_active_tool("hand");
    pump(40);
    expect(!bold->get_mapped(), "hand hides the options block");
    expect(toolbox.width_tracks_tool_grid(), "empty rail stays narrow");
    window.hide();
    pump(30);
  }

  // The canvas cursor follows the tool, and Space switches it to the hand.
  {
    Gtk::Window window;
    window.set_default_size(640, 480);
    CanvasView canvas;
    window.add(canvas);
    window.show_all();
    pump(80);
    expect(std::strcmp(canvas.canvas_cursor_name(), "default") == 0, "no tool keeps the default cursor");

    IdTool pencil("pencil");
    IdTool select("rect-select");
    IdTool lasso("lasso");
    IdTool text("text");
    IdTool hand("hand");
    canvas.set_tool(&pencil);
    expect(std::strcmp(canvas.canvas_cursor_name(), "crosshair") == 0, "pencil uses the crosshair");
    canvas.set_tool(&select);
    expect(std::strcmp(canvas.canvas_cursor_name(), "crosshair") == 0, "rect select uses the crosshair");
    canvas.set_tool(&lasso);
    expect(std::strcmp(canvas.canvas_cursor_name(), "crosshair") == 0, "lasso uses the crosshair");
    canvas.set_tool(&text);
    expect(std::strcmp(canvas.canvas_cursor_name(), "text") == 0, "text uses the I-beam");
    canvas.set_space_down(true);
    expect(std::strcmp(canvas.canvas_cursor_name(), "hand") == 0, "Space switches the cursor to the hand");
    canvas.set_space_down(false);
    expect(std::strcmp(canvas.canvas_cursor_name(), "text") == 0, "releasing Space restores the tool cursor");
    canvas.set_tool(&hand);
    expect(std::strcmp(canvas.canvas_cursor_name(), "hand") == 0, "the hand tool uses the hand");

    auto gdk_window = canvas.get_window();
    expect(static_cast<bool>(gdk_window), "canvas has a window for the cursor");
    if (gdk_window) {
      auto cursor = gdk_window->get_cursor();
      expect(static_cast<bool>(cursor) && cursor->get_cursor_type() == Gdk::HAND2,
             "the hand cursor is installed on the canvas window");
    }
    canvas.set_tool(&text);
    pump(20);
    if (gdk_window) {
      auto cursor = gdk_window->get_cursor();
      expect(static_cast<bool>(cursor) && cursor->get_cursor_type() == Gdk::XTERM,
             "the I-beam is installed on the canvas window");
    }
    window.hide();
    pump(30);
  }

  // Round 6: catalog tools highlight, square buttons, rail-sized width picker.
  {
    Gtk::Window window;
    window.set_default_size(280, 640);
    Toolbox toolbox;
    int count = 0;
    const lundukepaint::ToolboxTool* tools = lundukepaint::toolbox_tools(count);
    for (int i = 0; i < count; ++i) {
      toolbox.add_tool_button(tools[i].id, tools[i].tooltip, tools[i].icon);
    }
    window.add(toolbox);
    window.show_all();
    pump(160);
    toolbox.set_active_tool("curve");
    pump(40);
    expect(toolbox.tool_button_selected("curve"), "curve button follows the active tool");
    expect(!toolbox.tool_button_selected("pencil"), "pencil highlight cleared when curve is active");
    toolbox.set_active_tool("magic-wand");
    pump(20);
    expect(toolbox.tool_button_selected("magic-wand"), "magic wand button follows the key-selectable tool");
    expect(!toolbox.tool_button_selected("curve"), "previous tool highlight cleared");
    const int button_w = toolbox.tool_button_width();
    expect(button_w > 8 && button_w <= 36, "tool buttons stay square");
    expect(toolbox.width_tracks_tool_grid(), "tool grid does not stretch across the rail");
    expect(std::abs(toolbox.get_allocated_width() - 82) <= 1, "rail stays at the classic 82px");
    toolbox.set_active_tool("line");
    pump(40);
    const int picker_w = toolbox.line_width_picker_width();
    const int rail_w = toolbox.get_allocated_width();
    // Compact row from acd6af9: the picker matches the two tool columns (58 px),
    // inside the 82 px rail, rather than stretching to 160 px or more.
    expect(picker_w > 0 && picker_w <= rail_w, "line-width picker fits in the rail");
    expect(std::abs(picker_w - 58) <= 2, "line-width picker is the compact rail row");
    window.hide();
    pump(30);
  }

  // The rail stays at the pre-round-5 width at the default window size and
  // when the window is large, and nothing in it hangs out past that column.
  {
    Gtk::Window window;
    window.set_default_size(1100, 720);
    Gtk::Box row(Gtk::ORIENTATION_HORIZONTAL, 0);
    Toolbox toolbox;
    int count = 0;
    const lundukepaint::ToolboxTool* tools = lundukepaint::toolbox_tools(count);
    for (int i = 0; i < count; ++i) {
      toolbox.add_tool_button(tools[i].id, tools[i].tooltip, tools[i].icon);
    }
    Gtk::Box canvas_stand_in(Gtk::ORIENTATION_VERTICAL, 0);
    canvas_stand_in.set_hexpand(true);
    row.pack_start(toolbox, Gtk::PACK_SHRINK);
    row.pack_start(canvas_stand_in, Gtk::PACK_EXPAND_WIDGET);
    window.add(row);
    window.show_all();
    pump(160);

    const auto check_width = [](Toolbox& box, const char* when) {
      const int w = box.get_allocated_width();
      if (std::abs(w - kClassicRailWidth) > 1) {
        std::fprintf(stderr, "test_widgets: rail width %d %s, expected %d\n", w, when,
                     kClassicRailWidth);
        ++errors;
      }
    };
    expect(!toolbox.get_hexpand(), "the rail does not request extra horizontal space");
    check_width(toolbox, "at the default 1100x720 window");
    expect_rail_contains(toolbox, toolbox);
    expect_picker_naturals(toolbox, toolbox);

    toolbox.set_active_tool("line");
    pump(40);
    toolbox.set_active_tool("brush");
    pump(40);
    toolbox.set_active_tool("spray");
    pump(40);
    expect_rail_contains(toolbox, toolbox);
    expect_picker_naturals(toolbox, toolbox);

    const int before = toolbox.get_allocated_width();
    window.resize(1800, 1100);
    pump(160);
    check_width(toolbox, "at a large 1800x1100 window");
    expect(toolbox.get_allocated_width() == before, "the rail does not stretch when the window grows");
    expect_rail_contains(toolbox, toolbox);
    window.hide();
    pump(30);
  }

  // Zoom to Fit keeps a free ratio for an 800×600 picture in a wide window.
  {
    Gtk::Window window;
    window.set_default_size(1100, 720);
    Gtk::Box row(Gtk::ORIENTATION_HORIZONTAL, 0);
    Toolbox rail;
    rail.set_size_request(220, -1);
    CanvasView canvas;
    Gtk::Box dock(Gtk::ORIENTATION_VERTICAL, 0);
    dock.set_size_request(240, -1);
    row.pack_start(rail, Gtk::PACK_SHRINK);
    row.pack_start(canvas, Gtk::PACK_EXPAND_WIDGET);
    row.pack_start(dock, Gtk::PACK_SHRINK);
    window.add(row);
    window.show_all();
    pump(250);
    auto picture = Document::create(800, 600, Color::white());
    canvas.set_document(picture.get());
    canvas.zoom_fit();
    pump(80);
    const double fitted = canvas.zoom();
    std::printf("test_widgets: zoom-to-fit 800x600 -> %.4f\n", fitted);
    expect(fitted > 0.55 && fitted < 0.98, "zoom to fit is between 50% and 100%");
    expect(std::abs(fitted - 0.5) > 0.04 && std::abs(fitted - 1.0) > 0.04,
           "zoom to fit is not snapped to 50% or 100%");
    window.hide();
    pump(30);
  }

  // Opacity and blend for the current layer live on the layers panel.
  {
    Gtk::Window window;
    window.set_default_size(240, 420);
    LayersPanel panel;
    auto picture = Document::create(32, 32, Color::white());
    window.add(panel);
    panel.set_document(picture.get());
    window.show_all();
    pump(80);
    expect(panel.opacity_percent() == 100, "new layer shows 100% opacity");
    expect(panel.blend_text() == "Normal", "new layer shows Normal blend");
    picture->set_layer_opacity(0, 0.4f);
    panel.refresh();
    pump(30);
    expect(panel.opacity_percent() == 40, "panel shows the current layer opacity");
    window.hide();
    pump(20);
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_widgets: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_widgets: ok\n");
  return 0;
}
