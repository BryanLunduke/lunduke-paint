// SPDX-License-Identifier: GPL-3.0-or-later
//
// Widget-level checks that need a real X display. Exits 77 (meson "SKIP") when
// there is none, so the headless suite stays green; run it under
// `xvfb-run -a meson test -C build widgets` (or an existing DISPLAY) to
// actually exercise:
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
#include "ui/canvas_view.hpp"
#include "ui/intro_howdy.hpp"
#include "ui/toolbox.hpp"

#include <glib.h>
#include <gtk/gtk.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>

#include <cstdio>
#include <vector>

namespace {

using lundukepaint::CanvasView;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Layer;
using lundukepaint::Toolbox;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_widgets: %s\n", msg);
    ++errors;
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
    // FG/BG wells live on the MacPaint-style pattern strip (0.5-3), not the toolbox.
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
      int win_x = 0;
      int win_y = 0;
      if (auto win = window.get_window()) {
        win->get_origin(win_x, win_y);
        expect(x0 >= win_x && y0 >= win_y, "canvas origin is inside the window");
        expect(x0 <= win_x + window.get_allocated_width() &&
                   y0 <= win_y + window.get_allocated_height(),
               "canvas origin is not off the window");
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

  if (errors != 0) {
    std::fprintf(stderr, "test_widgets: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_widgets: ok\n");
  return 0;
}
