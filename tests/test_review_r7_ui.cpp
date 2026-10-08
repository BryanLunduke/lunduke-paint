// SPDX-License-Identifier: GPL-3.0-or-later
//
// Round 7, on a display: the main window's minimum fits a 768-tall screen
// with a panel, nothing is cut off at a 1280×800-fitting size, Enter confirms
// every dialog, the History list tells the truth when undo steps are dropped,
// and Save Image shows JPEG quality only for a JPEG. A missing display is a
// skip (exit 77) unless LUNDUKEPAINT_REQUIRE_DISPLAY is set, in which case it
// is a failure. Optional argv[1] is a directory for PNG screenshots.

#include "app/main_window.hpp"
#include "app/preferences.hpp"
#include "app/save_dialog.hpp"
#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/history.hpp"
#include "doc/layer.hpp"
#include "ui/color_well.hpp"
#include "ui/dialogs_adjust.hpp"
#include "ui/dialogs_image.hpp"
#include "ui/dialogs_new.hpp"
#include "ui/dialogs_prefs.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <unistd.h>

#include <gdkmm/pixbuf.h>
#include <gdkmm/screen.h>
#include <glib.h>
#include <gtk/gtk.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/label.h>
#include <gtkmm/main.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>

namespace {

using lundukepaint::BlurDialog;
using lundukepaint::BrightnessContrastDialog;
using lundukepaint::CanvasSizeDialog;
using lundukepaint::Color;
using lundukepaint::HueSaturationDialog;
using lundukepaint::ImageFormat;
using lundukepaint::MainWindow;
using lundukepaint::NewImageDialog;
using lundukepaint::PixelPatchCommand;
using lundukepaint::PosterizeDialog;
using lundukepaint::Preferences;
using lundukepaint::PreferencesDialog;
using lundukepaint::Rect;
using lundukepaint::ScaleImageDialog;
using lundukepaint::add_save_filters;
using lundukepaint::format_of_save_filter;
using lundukepaint::kUndoDroppedNotice;
using lundukepaint::watch_jpeg_quality;
using lundukepaint::watch_save_filter;

int errors = 0;
std::string shot_dir;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_review_r7_ui: %s\n", msg);
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

void walk(Gtk::Widget& widget, const std::function<void(Gtk::Widget&)>& fn) {
  fn(widget);
  auto* container = dynamic_cast<Gtk::Container*>(&widget);
  if (container == nullptr) {
    return;
  }
  const std::vector<Gtk::Widget*> children = container->get_children();
  for (Gtk::Widget* child : children) {
    if (child != nullptr && child != &widget) {
      walk(*child, fn);
    }
  }
}

Gtk::SpinButton* first_spin(Gtk::Widget& root) {
  Gtk::SpinButton* found = nullptr;
  walk(root, [&](Gtk::Widget& widget) {
    if (found == nullptr) {
      found = dynamic_cast<Gtk::SpinButton*>(&widget);
    }
  });
  return found;
}

Gtk::Widget* find_named(Gtk::Widget& root, const char* name) {
  Gtk::Widget* found = nullptr;
  walk(root, [&](Gtk::Widget& widget) {
    if (found == nullptr && widget.get_name() == name) {
      found = &widget;
    }
  });
  return found;
}

bool contains_text(Gtk::Widget& root, const char* text) {
  bool found = false;
  walk(root, [&](Gtk::Widget& widget) {
    auto* label = dynamic_cast<Gtk::Label*>(&widget);
    if (label != nullptr && label->get_text() == text) {
      found = true;
    }
  });
  return found;
}

bool inside_toplevel(Gtk::Widget& top, Gtk::Widget& widget) {
  int x = 0;
  int y = 0;
  if (!widget.get_mapped() || !widget.get_visible()) {
    return true;
  }
  if (!widget.translate_coordinates(top, 0, 0, x, y)) {
    return true;
  }
  const int w = widget.get_allocated_width();
  const int h = widget.get_allocated_height();
  if (w < 1 || h < 1) {
    return true;
  }
  const int tw = top.get_allocated_width();
  const int th = top.get_allocated_height();
  return x >= -1 && y >= -1 && x + w <= tw + 1 && y + h <= th + 1;
}

bool scrolled_ancestor_on_screen(Gtk::Widget& top, Gtk::Widget& widget) {
  for (Gtk::Widget* parent = widget.get_parent(); parent != nullptr && parent != &top;
       parent = parent->get_parent()) {
    if (!GTK_IS_SCROLLED_WINDOW(parent->gobj())) {
      continue;
    }
    if (inside_toplevel(top, *parent) && parent->get_allocated_width() > 1 &&
        parent->get_allocated_height() > 1) {
      return true;
    }
  }
  return false;
}

void save_screen(const std::string& path) {
  if (shot_dir.empty()) {
    return;
  }
  pump(40);
  try {
    auto screen = Gdk::Screen::get_default();
    auto root = screen->get_root_window();
    const int w = screen->get_width();
    const int h = screen->get_height();
    auto pix = Gdk::Pixbuf::create(root, 0, 0, w, h);
    pix->save(path, "png");
    std::printf("test_review_r7_ui: wrote %s (%dx%d)\n", path.c_str(), w, h);
  } catch (const Glib::Error& err) {
    std::fprintf(stderr, "test_review_r7_ui: screen shot %s: %s\n", path.c_str(), err.what().c_str());
    ++errors;
  }
}

void save_png(Gtk::Widget& widget, const std::string& path) {
  if (shot_dir.empty()) {
    return;
  }
  pump(80);
  auto window = widget.get_window();
  if (!window) {
    std::fprintf(stderr, "test_review_r7_ui: no gdk window for %s\n", path.c_str());
    return;
  }
  const int w = widget.get_allocated_width();
  const int h = widget.get_allocated_height();
  if (w < 2 || h < 2) {
    return;
  }
  try {
    auto pix = Gdk::Pixbuf::create(window, 0, 0, w, h);
    pix->save(path, "png");
    std::printf("test_review_r7_ui: wrote %s (%dx%d)\n", path.c_str(), w, h);
  } catch (const Glib::Error& err) {
    std::fprintf(stderr, "test_review_r7_ui: screenshot %s: %s\n", path.c_str(), err.what().c_str());
    ++errors;
  }
}

bool default_response_is(Gtk::Dialog& dialog, int response) {
  Gtk::Widget* button = dialog.get_widget_for_response(response);
  return button != nullptr && button == dialog.get_default_widget();
}

void check_dialog_entries(Gtk::Dialog& dialog, const char* title) {
  if (!default_response_is(dialog, GTK_RESPONSE_OK) &&
      !default_response_is(dialog, GTK_RESPONSE_ACCEPT) &&
      !default_response_is(dialog, GTK_RESPONSE_YES)) {
    std::fprintf(stderr, "test_review_r7_ui: %s default widget is not OK\n", title);
    ++errors;
  }
  int entries = 0;
  walk(dialog, [&](Gtk::Widget& widget) {
    auto* entry = dynamic_cast<Gtk::Entry*>(&widget);
    if (entry == nullptr) {
      return;
    }
    ++entries;
    if (!entry->get_activates_default()) {
      std::fprintf(stderr, "test_review_r7_ui: %s entry/spin does not activate the default\n", title);
      ++errors;
    }
  });
  if (entries < 1) {
    std::fprintf(stderr, "test_review_r7_ui: %s has no entry or spin\n", title);
    ++errors;
  }
}

void expect_enter_accepts(Gtk::Dialog& dialog, Gtk::SpinButton* entry, const char* title) {
  if (entry == nullptr) {
    std::fprintf(stderr, "test_review_r7_ui: %s has no spin button\n", title);
    ++errors;
    return;
  }
  check_dialog_entries(dialog, title);
  int response = GTK_RESPONSE_NONE;
  dialog.signal_response().connect([&response](int value) { response = value; });
  dialog.present();
  pump(40);
  entry->grab_focus();
  pump(20);
  // Return is bound to the entry "activate" signal, which honors
  // activates-default. GtkButton then waits 250 ms before emitting clicked.
  g_signal_emit_by_name(entry->gobj(), "activate");
  pump(400);
  if (response != GTK_RESPONSE_OK) {
    std::fprintf(stderr, "test_review_r7_ui: Enter in %s response %d\n", title, response);
    ++errors;
  }
  dialog.hide();
}

struct ModalGrab {
  const char* title = nullptr;
  bool seen = false;
  int entries = 0;
};

gboolean dismiss_modal(gpointer data) {
  auto* grab = static_cast<ModalGrab*>(data);
  GList* tops = gtk_window_list_toplevels();
  for (GList* item = tops; item != nullptr; item = item->next) {
    auto* window = GTK_WINDOW(item->data);
    if (!gtk_widget_get_visible(GTK_WIDGET(window))) {
      continue;
    }
    const char* title = gtk_window_get_title(window);
    if (title == nullptr || std::strcmp(title, grab->title) != 0) {
      continue;
    }
    grab->seen = true;
    if (GTK_IS_DIALOG(window)) {
      auto* dialog = dynamic_cast<Gtk::Dialog*>(Glib::wrap(GTK_WIDGET(window)));
      if (dialog == nullptr || (!default_response_is(*dialog, GTK_RESPONSE_OK) &&
                                !default_response_is(*dialog, GTK_RESPONSE_ACCEPT))) {
        std::fprintf(stderr, "test_review_r7_ui: %s default widget is not OK\n", grab->title);
        ++errors;
      }
    }
    walk(*Glib::wrap(GTK_WIDGET(window)), [&](Gtk::Widget& widget) {
      auto* entry = dynamic_cast<Gtk::Entry*>(&widget);
      if (entry == nullptr) {
        return;
      }
      ++grab->entries;
      if (!entry->get_activates_default()) {
        std::fprintf(stderr, "test_review_r7_ui: %s control does not activate the default\n",
                     grab->title);
        ++errors;
      }
    });
    if (GTK_IS_DIALOG(window)) {
      gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_CANCEL);
    }
  }
  g_list_free(tops);
  return G_SOURCE_REMOVE;
}

void commit_dot(lundukepaint::Document& document, const char* name, std::uint8_t red) {
  auto& layer = document.layers().active_layer();
  std::unique_ptr<lundukepaint::Layer> before = layer.clone();
  Color color = Color::white();
  color.r = red;
  layer.set_pixel(2, 2, color);
  auto command = PixelPatchCommand::from_layers(*before, layer, Rect{0, 0, layer.width(), layer.height()}, name,
                                                document.layers().active_index());
  document.history().commit_applied(std::move(command));
}

std::string temp_dir(const char* prefix) {
  char tmpl[64];
  std::snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
  char* made = mkdtemp(tmpl);
  return made == nullptr ? std::string() : std::string(made);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') {
    shot_dir = argv[1];
  }
  const std::string config_home = temp_dir("lp-r7cfg-");
  const std::string state_home = temp_dir("lp-r7st-");
  if (!config_home.empty()) {
    g_setenv("XDG_CONFIG_HOME", config_home.c_str(), TRUE);
  }
  if (!state_home.empty()) {
    g_setenv("XDG_STATE_HOME", state_home.c_str(), TRUE);
  }

  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_review_r7_ui: display required but gtk_init_check failed\n");
      return 1;
    }
    std::printf("test_review_r7_ui: no display, skipping\n");
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();

  // 768 work area minus a typical panel, then a title bar. The review asked
  // for a minimum under about 680 so the window still fits with a taller panel.
  constexpr int kPanel = 36;
  constexpr int kTitle = 28;
  constexpr int kMinHeightCap = 680;
  constexpr int kNaturalHeightCap = 768 - kPanel;
  constexpr int kWidthCap = 1280;
  constexpr int kClientOn800 = 800 - kPanel - kTitle;

  MainWindow window;
  pump(60);

  int min_w = 0;
  int nat_w = 0;
  int min_h = 0;
  int nat_h = 0;
  window.get_preferred_width(min_w, nat_w);
  window.get_preferred_height(min_h, nat_h);
  int min_for_width = 0;
  int nat_for_width = 0;
  window.get_preferred_height_for_width(std::min(nat_w, kWidthCap), min_for_width, nat_for_width);
  std::printf("test_review_r7_ui: preferred %dx%d natural %dx%d height-for-width %d/%d\n", min_w,
              min_h, nat_w, nat_h, min_for_width, nat_for_width);
  expect(min_h > 0 && min_h <= kMinHeightCap, "minimum height fits under 680");
  expect(nat_h > 0 && nat_h <= kNaturalHeightCap, "natural height fits 768 minus a panel");
  expect(min_for_width > 0 && min_for_width <= kMinHeightCap,
         "minimum height at a 1280-fitting width is under 680");
  expect(min_w > 0 && min_w <= kWidthCap, "minimum width fits 1280");
  expect(nat_w > 0 && nat_w <= kWidthCap, "natural width fits 1280");

  const int open_w = std::min(1100, kWidthCap);
  const int open_h = std::min(std::max(nat_h, 480), kClientOn800);
  window.resize(open_w, open_h);
  pump(120);
  int got_w = 0;
  int got_h = 0;
  window.get_size(got_w, got_h);
  std::printf("test_review_r7_ui: opened %dx%d (asked %dx%d)\n", got_w, got_h, open_w, open_h);
  expect(got_w <= kWidthCap, "opened width fits 1280");
  expect(got_h <= kClientOn800, "opened height fits 1280x800 with a panel and title bar");
  expect(got_h <= 768 - kPanel, "opened height fits 768 minus a panel");

  int rail_w = -1;
  Gtk::Widget* rail = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    if (widget.get_tooltip_text() != lundukepaint::kForegroundWellTooltip) {
      return;
    }
    for (Gtk::Widget* parent = &widget; parent != nullptr; parent = parent->get_parent()) {
      const int width = parent->get_allocated_width();
      if (std::abs(width - 82) <= 1 && parent->get_parent() != nullptr &&
          parent->get_parent()->get_allocated_width() > 200) {
        rail = parent;
        rail_w = width;
        break;
      }
    }
  });
  expect(rail != nullptr && std::abs(rail_w - 82) <= 1, "left tool rail stays 82 px");
  if (rail != nullptr) {
    walk(*rail, [&](Gtk::Widget& widget) {
      auto* label = dynamic_cast<Gtk::Label*>(&widget);
      if (label == nullptr || !label->get_visible()) {
        return;
      }
      const Glib::ustring text = label->get_text();
      if (text.find("Foreground") != Glib::ustring::npos ||
          text.find("Background") != Glib::ustring::npos || text == "Swap" || text == "Reset") {
        std::fprintf(stderr, "test_review_r7_ui: visible color label '%s'\n", text.c_str());
        ++errors;
      }
    });
  }

  const char* must_show[] = {
      lundukepaint::kForegroundWellTooltip,
      lundukepaint::kBackgroundWellTooltip,
      lundukepaint::kSwapColorsTooltip,
      lundukepaint::kResetColorsTooltip,
  };
  for (const char* tip : must_show) {
    Gtk::Widget* found = nullptr;
    walk(window, [&](Gtk::Widget& widget) {
      if (found == nullptr && widget.get_tooltip_text() == tip) {
        found = &widget;
      }
    });
    if (found == nullptr) {
      std::fprintf(stderr, "test_review_r7_ui: missing %s\n", tip);
      ++errors;
      continue;
    }
    if (!inside_toplevel(window, *found)) {
      std::fprintf(stderr, "test_review_r7_ui: %s is cut off\n", tip);
      ++errors;
    }
  }
  Gtk::Widget* patterns = nullptr;
  Gtk::Widget* status = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    auto* label = dynamic_cast<Gtk::Label*>(&widget);
    if (label == nullptr) {
      return;
    }
    if (label->get_text() == "Patterns") {
      patterns = label->get_parent() != nullptr ? static_cast<Gtk::Widget*>(label->get_parent())
                                                : static_cast<Gtk::Widget*>(label);
    }
    if (label->get_text().find("×") != Glib::ustring::npos &&
        label->get_text().find("sel") == Glib::ustring::npos) {
      status = label->get_parent();
    }
  });
  expect(patterns != nullptr && inside_toplevel(window, *patterns), "pattern strip is fully on screen");
  expect(status != nullptr && inside_toplevel(window, *status), "status bar is fully on screen");

  walk(window, [&](Gtk::Widget& widget) {
    if (!widget.get_visible() || !widget.get_mapped()) {
      return;
    }
    if (widget.get_allocated_width() < 1 || widget.get_allocated_height() < 1) {
      return;
    }
    if (&widget == &window || inside_toplevel(window, widget)) {
      return;
    }
    if (scrolled_ancestor_on_screen(window, widget)) {
      return;
    }
    std::fprintf(stderr, "test_review_r7_ui: %s cut off at %s\n", G_OBJECT_TYPE_NAME(widget.gobj()),
                 widget.get_tooltip_text().c_str());
    ++errors;
  });

  // The clipping check used a short window. The screenshot is the size the
  // program opens at, which still fits a 1280×800 screen with a panel.
  window.resize(1100, 640);
  window.move(40, 32);
  Gtk::Window panel;
  if (!shot_dir.empty()) {
    auto screen = Gdk::Screen::get_default();
    panel.set_title("panel");
    panel.set_type_hint(Gdk::WINDOW_TYPE_HINT_DOCK);
    panel.set_decorated(false);
    panel.set_default_size(screen->get_width(), kPanel);
    panel.move(0, std::max(0, screen->get_height() - kPanel));
    auto* label = Gtk::manage(new Gtk::Label("Panel"));
    panel.add(*label);
    panel.show_all();
  }
  pump(80);
  save_png(window, shot_dir + "/paint-window.png");
  save_screen(shot_dir + "/paint-1280x800-fits.png");

  {
    NewImageDialog dialog(window, 640, 480);
    expect_enter_accepts(dialog, first_spin(dialog), "New Image");
  }
  {
    CanvasSizeDialog dialog(window, 640, 480);
    expect_enter_accepts(dialog, first_spin(dialog), "Canvas Size");
  }
  {
    ScaleImageDialog dialog(window, 640, 480);
    expect_enter_accepts(dialog, first_spin(dialog), "Scale Image");
  }
  {
    Preferences prefs;
    PreferencesDialog dialog(window, prefs);
    expect_enter_accepts(dialog, first_spin(dialog), "Preferences");
  }
  {
    PosterizeDialog dialog(window);
    expect_enter_accepts(dialog, first_spin(dialog), "Posterize");
  }
  {
    BlurDialog dialog(window);
    expect_enter_accepts(dialog, first_spin(dialog), "Blur");
  }
  {
    BrightnessContrastDialog dialog(window);
    expect(default_response_is(dialog, GTK_RESPONSE_OK), "Brightness / Contrast default is OK");
    dialog.hide();
  }
  {
    HueSaturationDialog dialog(window);
    expect(default_response_is(dialog, GTK_RESPONSE_OK), "Hue / Saturation default is OK");
    dialog.hide();
  }

  ModalGrab props;
  props.title = "Layer properties";
  g_idle_add(dismiss_modal, &props);
  window.action_layer_properties();
  expect(props.seen && props.entries >= 4, "Layer properties spins and name activate the default");

  ModalGrab rename;
  rename.title = "Rename layer";
  Gtk::Button* rename_button = nullptr;
  walk(window, [&](Gtk::Widget& widget) {
    auto* button = dynamic_cast<Gtk::Button*>(&widget);
    if (button != nullptr && button->get_tooltip_text() == "Rename layer") {
      rename_button = button;
    }
  });
  expect(rename_button != nullptr, "rename button is on the layers panel");
  if (rename_button != nullptr) {
    g_idle_add(dismiss_modal, &rename);
    rename_button->clicked();
  }
  expect(rename.seen && rename.entries >= 1, "Rename layer entry activates the default");

  window.document().history().set_byte_cap(1);
  commit_dot(window.document(), "first", 10);
  expect(window.document().history().count() == 1, "a single step is kept even under a tiny cap");
  commit_dot(window.document(), "second", 40);
  expect(window.document().history().count() == 1, "the newest step is the one that stays");
  expect(window.document().history().base_dropped(), "dropping older steps is recorded");
  expect(std::strcmp(window.document().history().base_label(), "Start of history") == 0,
         "History names the oldest restorable state");
  window.update_chrome();
  pump(40);
  expect(contains_text(window, kUndoDroppedNotice), "dropped steps are announced");
  expect(contains_text(window, "Start of history"), "History shows Start of history");
  expect(!contains_text(window, "New document"), "History does not claim the original document");
  save_png(window, shot_dir + "/paint-history-window.png");
  save_screen(shot_dir + "/paint-history-undo-cap.png");

  struct SaveProbe {
    bool ora = false;
    bool png = false;
    bool jpeg = false;
    bool typed_png = false;
    bool spin = false;
    bool seen = false;
  } probe;
  g_idle_add(
      +[](gpointer data) -> gboolean {
        auto* probe = static_cast<SaveProbe*>(data);
        GList* tops = gtk_window_list_toplevels();
        for (GList* item = tops; item != nullptr; item = item->next) {
          auto* gtk_window = GTK_WINDOW(item->data);
          const char* title = gtk_window_get_title(gtk_window);
          if (title == nullptr || std::strcmp(title, "Save Image") != 0 ||
              !GTK_IS_FILE_CHOOSER_DIALOG(gtk_window)) {
            continue;
          }
          auto* dialog = dynamic_cast<Gtk::FileChooserDialog*>(Glib::wrap(GTK_WIDGET(gtk_window)));
          if (dialog == nullptr) {
            continue;
          }
          probe->seen = true;
          Gtk::Widget* row = find_named(*dialog, "jpeg-quality-row");
          Glib::RefPtr<Gtk::FileFilter> png;
          Glib::RefPtr<Gtk::FileFilter> jpeg;
          for (const auto& filter : dialog->list_filters()) {
            const Glib::ustring name = filter->get_name();
            if (name.find("PNG") != Glib::ustring::npos) {
              png = filter;
            }
            if (name.find("JPEG") != Glib::ustring::npos) {
              jpeg = filter;
            }
          }
          if (row != nullptr) {
            probe->ora = !row->get_visible();
            save_png(*dialog, shot_dir + "/save-dialog-ora.png");
            save_screen(shot_dir + "/save-dialog-ora-screen.png");
            if (png) {
              dialog->set_filter(png);
              probe->png = !row->get_visible();
              save_png(*dialog, shot_dir + "/save-dialog-png.png");
              save_screen(shot_dir + "/save-dialog-png-screen.png");
            }
            if (jpeg) {
              dialog->set_filter(jpeg);
              probe->jpeg = row->get_visible();
              save_png(*dialog, shot_dir + "/save-dialog-jpeg.png");
              save_screen(shot_dir + "/save-dialog-jpeg-screen.png");
            }
            dialog->set_current_name("flat-test.png");
            probe->typed_png = !row->get_visible();
            Gtk::SpinButton* spin = first_spin(*row);
            probe->spin = spin != nullptr && spin->get_activates_default();
          }
          gtk_dialog_response(GTK_DIALOG(gtk_window), GTK_RESPONSE_CANCEL);
        }
        g_list_free(tops);
        return G_SOURCE_REMOVE;
      },
      &probe);
  window.action_save_as();
  expect(probe.seen, "Save Image dialog opened");
  expect(probe.ora, "JPEG quality is hidden for OpenRaster");
  expect(probe.png, "JPEG quality is hidden for PNG");
  expect(probe.jpeg, "JPEG quality is shown for JPEG");
  expect(probe.typed_png, "a typed .png hides JPEG quality");
  expect(probe.spin, "JPEG quality spin activates the default");

  if (errors != 0) {
    std::fprintf(stderr, "test_review_r7_ui: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_review_r7_ui: ok\n");
  return 0;
}
