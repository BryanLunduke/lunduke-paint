// SPDX-License-Identifier: GPL-3.0-or-later
// Second 1.1.0 review, on a display: rail labels, brush size readout,
// brush workshop mnemonic, and the CRT overlay at 200%.

#include "app/main_window.hpp"
#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "ui/canvas_view.hpp"
#include "ui/toolbox.hpp"

#include <gdk/gdk.h>
#include <gtk/gtk.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/label.h>
#include <gtkmm/main.h>
#include <gtkmm/offscreenwindow.h>
#include <gtkmm/window.h>

#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

namespace {

int errors = 0;

void expect(bool cond, const char* msg) {
  std::printf("%s: %s\n", cond ? "PASS" : "FAIL", msg);
  if (!cond) {
    ++errors;
  }
}

void pump(int ms) {
  const gint64 end = g_get_monotonic_time() + static_cast<gint64>(ms) * 1000;
  while (g_get_monotonic_time() < end) {
    while (gtk_events_pending()) {
      gtk_main_iteration();
    }
    g_usleep(10000);
  }
}

void walk(Gtk::Widget& widget, const std::function<void(Gtk::Widget&)>& fn) {
  fn(widget);
  if (auto* container = dynamic_cast<Gtk::Container*>(&widget)) {
    for (Gtk::Widget* child : container->get_children()) {
      if (child != nullptr) {
        walk(*child, fn);
      }
    }
  }
}

Gtk::Label* check_label(Gtk::Widget& root, const char* text) {
  Gtk::Label* found = nullptr;
  walk(root, [&](Gtk::Widget& widget) {
    auto* button = dynamic_cast<Gtk::CheckButton*>(&widget);
    if (button == nullptr) {
      return;
    }
    auto* label = dynamic_cast<Gtk::Label*>(button->get_child());
    if (label != nullptr && label->get_text() == text) {
      found = label;
    }
  });
  return found;
}

bool label_fits(Gtk::Label* label) {
  if (label == nullptr || !label->get_mapped()) {
    return false;
  }
  auto layout = label->get_layout();
  return layout && !layout->is_ellipsized();
}

void send_bracket(Gtk::Widget& widget) {
  GdkEvent* event = gdk_event_new(GDK_KEY_PRESS);
  auto* key = reinterpret_cast<GdkEventKey*>(event);
  key->window = widget.get_window()->gobj();
  g_object_ref(key->window);
  key->send_event = TRUE;
  key->time = GDK_CURRENT_TIME;
  key->keyval = GDK_KEY_bracketright;
  key->state = 0;
  key->length = 0;
  key->string = nullptr;
  gtk_widget_event(GTK_WIDGET(widget.gobj()), event);
  gdk_event_free(event);
}

struct WorkshopProbe {
  bool saw = false;
  bool underline = false;
};

gboolean probe_workshop(gpointer data) {
  auto* probe = static_cast<WorkshopProbe*>(data);
  GList* tops = gtk_window_list_toplevels();
  for (GList* item = tops; item != nullptr; item = item->next) {
    auto* top = GTK_WIDGET(item->data);
    if (!gtk_widget_get_visible(top)) {
      continue;
    }
    const char* title = gtk_window_get_title(GTK_WINDOW(top));
    if (title == nullptr || std::strcmp(title, "Brush Workshop") != 0) {
      continue;
    }
    probe->saw = true;
    std::function<void(GtkWidget*)> walk_gtk = [&](GtkWidget* widget) {
      if (GTK_IS_BUTTON(widget)) {
        const char* label = gtk_button_get_label(GTK_BUTTON(widget));
        if (label != nullptr && std::strcmp(label, "_Undo") == 0) {
          probe->underline = gtk_button_get_use_underline(GTK_BUTTON(widget)) == TRUE;
        }
      }
      if (GTK_IS_CONTAINER(widget)) {
        GList* children = gtk_container_get_children(GTK_CONTAINER(widget));
        for (GList* child = children; child != nullptr; child = child->next) {
          walk_gtk(GTK_WIDGET(child->data));
        }
        g_list_free(children);
      }
    };
    walk_gtk(top);
    gtk_dialog_response(GTK_DIALOG(top), GTK_RESPONSE_CANCEL);
  }
  g_list_free(tops);
  return G_SOURCE_REMOVE;
}

void test_crt() {
  auto doc = lundukepaint::Document::create(32, 24, lundukepaint::Color{176, 184, 196, 255});
  // Offscreen so a parallel test's window cannot cover the sample.
  Gtk::OffscreenWindow window;
  window.set_default_size(280, 220);
  lundukepaint::CanvasView canvas;
  window.add(canvas);
  window.show_all();
  canvas.set_document(doc.get());
  canvas.set_zoom(2.0);
  canvas.set_scanlines(true);
  canvas.invalidate_all();
  pump(400);
  const int w = canvas.get_allocated_width();
  const int h = canvas.get_allocated_height();
  auto pixbuf = Gdk::Pixbuf::create(canvas.get_window(), 0, 0, w, h);
  expect(static_cast<bool>(pixbuf), "crt pixbuf");
  if (!pixbuf) {
    window.hide();
    return;
  }
  auto luma_at = [&](int x, int y) {
    guchar* p = pixbuf->get_pixels() + static_cast<std::size_t>(y) * pixbuf->get_rowstride() +
                static_cast<std::size_t>(x) * pixbuf->get_n_channels();
    return static_cast<int>(p[0]) + p[1] + p[2];
  };
  bool paired = false;
  bool saw_rows = false;
  for (int x = 8; x < w - 8 && !paired; x += 12) {
    int run_start = -1;
    int best_start = -1;
    int best_len = 0;
    int current = -1;
    for (int y = 0; y <= h; ++y) {
      const bool on = y < h && luma_at(x, y) < 620;
      if (on && current < 0) {
        current = y;
      }
      if (!on && current >= 0) {
        const int len = y - current;
        if (len > best_len) {
          best_len = len;
          best_start = current;
        }
        current = -1;
      }
    }
    run_start = best_start;
    if (best_len < 16) {
      continue;
    }
    saw_rows = true;
    const int last = run_start + best_len - 1;
    auto bands = [&](int start) {
      int groups = 0;
      for (int y = start; y + 3 <= last; y += 4) {
        const int a = luma_at(x, y);
        const int b = luma_at(x, y + 1);
        const int c = luma_at(x, y + 2);
        const int d = luma_at(x, y + 3);
        if (std::abs(a - b) > 18 || std::abs(c - d) > 18 || c + 24 >= a) {
          return false;
        }
        ++groups;
      }
      return groups >= 3;
    };
    paired = bands(run_start) || bands(run_start + 1);
  }
  expect(saw_rows, "crt overlay has rows");
  expect(paired, "crt overlay at 200% darkens whole image rows");
  window.hide();
  pump(20);
}

Gtk::Label* size_label(Gtk::Widget& root) {
  Gtk::Label* found = nullptr;
  walk(root, [&](Gtk::Widget& widget) {
    auto* label = dynamic_cast<Gtk::Label*>(&widget);
    if (label != nullptr && label->get_text().find(" px") != Glib::ustring::npos) {
      found = label;
    }
  });
  return found;
}

}  // namespace

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_review_r17_ui: display required\n");
      return 1;
    }
    std::printf("test_review_r17_ui: no display, skipping\n");
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();

  test_crt();

  lundukepaint::MainWindow window;
  window.set_default_size(980, 680);
  window.show_all();
  pump(150);

  window.set_active_tool("brush");
  pump(80);
  Gtk::Label* smear = check_label(window, "Smr");
  expect(smear != nullptr, "brush rail says Smr");
  expect(label_fits(smear), "Smr is not cut off");
  if (smear != nullptr) {
    expect(smear->get_parent()->get_tooltip_text().find("Smear") != Glib::ustring::npos,
           "Smr tooltip names Smear");
  }
  Gtk::Label* size = size_label(window);
  expect(size != nullptr && size->get_text() == "8 px", "brush rail shows the size");
  const std::string before = size != nullptr ? size->get_text().raw() : "";
  send_bracket(window);
  pump(40);
  size = size_label(window);
  expect(size != nullptr && size->get_text() != before && size->get_text().find(" px") != Glib::ustring::npos,
         "brush rail updates after ]");

  window.set_active_tool("rect-select");
  pump(60);
  Gtk::Label* trans = check_label(window, "Trns");
  expect(trans != nullptr, "select rail says Trns");
  expect(label_fits(trans), "Trns is not cut off");
  if (trans != nullptr) {
    expect(trans->get_parent()->get_tooltip_text().find("Transparent") != Glib::ustring::npos,
           "Trns tooltip names Transparent move");
  }

  window.set_active_tool("line");
  pump(60);
  Gtk::Label* aa = check_label(window, "AA");
  expect(aa != nullptr, "shape rail says AA");
  expect(label_fits(aa), "AA is not cut off");
  if (aa != nullptr) {
    expect(aa->get_parent()->get_tooltip_text().find("Anti-alias") != Glib::ustring::npos,
           "AA tooltip names Anti-alias");
  }
  int rail_width = 0;
  walk(window, [&](Gtk::Widget& widget) {
    if (dynamic_cast<lundukepaint::Toolbox*>(&widget) != nullptr) {
      rail_width = widget.get_allocated_width();
    }
  });
  expect(std::abs(rail_width - 82) <= 2, "rail stays 82 px");

  lundukepaint::Layer& layer = window.document().layers().active_layer();
  for (int y = 4; y < 20; ++y) {
    for (int x = 4; x < 20; ++x) {
      layer.set_pixel(x, y, {20, 40, 180, 255});
    }
  }
  window.document().selection().set_rect({4, 4, 16, 16});
  window.action_selection_brush();
  WorkshopProbe probe;
  g_timeout_add(150, probe_workshop, &probe);
  window.action_brush_workshop();
  pump(40);
  expect(probe.saw, "brush workshop opened");
  expect(probe.underline, "workshop Undo uses an underline mnemonic");

  window.hide();
  if (errors != 0) {
    std::fprintf(stderr, "test_review_r17_ui: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_review_r17_ui: ok\n");
  return 0;
}
