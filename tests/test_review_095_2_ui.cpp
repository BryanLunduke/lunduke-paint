// SPDX-License-Identifier: GPL-3.0-or-later
// 0.9.5-2 pre-1.0 fixes, on a display:
//   1. Two open images show the tab bar; switching tabs switches documents;
//      closing back to one hides it again.
//   2. An outside open (file manager, command line) that arrives while Save
//      As is up is held until the dialog closes, and Save As writes the
//      document it was opened for, even if something else switched tabs.
//   3. Ctrl+S and the close prompt on a GIF go to Save As with PNG proposed;
//      the close prompt closes only after the save succeeded.
//   4. The large-image warning comes before the decode; Cancel decodes nothing.
// A missing display is a skip (77) unless LUNDUKEPAINT_REQUIRE_DISPLAY.

#include "app/main_window.hpp"
#include "io/image_io.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <glib.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <gtkmm/main.h>
#include <gtkmm/notebook.h>

using namespace lundukepaint;

namespace {
int errors = 0;
void expect(bool cond, const char* msg) {
  std::printf("%s: %s\n", cond ? "PASS" : "FAIL", msg);
  if (!cond) ++errors;
}
std::string temp_dir(const char* prefix) {
  std::string t = std::string("/tmp/") + prefix + "XXXXXX";
  std::vector<char> buf(t.begin(), t.end());
  buf.push_back('\0');
  return mkdtemp(buf.data()) ? std::string(buf.data()) : std::string();
}
bool pump_until(const std::function<bool()>& ready, int ms) {
  const gint64 end = g_get_monotonic_time() + static_cast<gint64>(ms) * 1000;
  for (;;) {
    while (gtk_events_pending()) gtk_main_iteration();
    if (ready()) return true;
    if (g_get_monotonic_time() >= end) return false;
    g_usleep(20000);
  }
}
bool write_png(const std::string& path, int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * h * 4);
  for (std::size_t i = 0; i < px.size(); i += 4) {
    px[i] = r;
    px[i + 1] = g;
    px[i + 2] = b;
    px[i + 3] = 255;
  }
  std::string err;
  return save_flat_image(path, ImageFormat::Png, px.data(), w, h, w * 4, 90, err);
}
// First pixel of a saved file, or -1s when it cannot be read.
void first_pixel(const std::string& path, int& r, int& g, int& b) {
  r = g = b = -1;
  LoadedImage img = load_flat_image(path);
  if (img.ok() && !img.rgba.empty()) {
    r = img.rgba[0];
    g = img.rgba[1];
    b = img.rgba[2];
  }
}
Gtk::Notebook* find_notebook(Gtk::Widget& root) {
  if (auto* nb = dynamic_cast<Gtk::Notebook*>(&root)) return nb;
  if (auto* box = dynamic_cast<Gtk::Container*>(&root)) {
    for (Gtk::Widget* child : box->get_children()) {
      if (child == nullptr) continue;
      if (Gtk::Notebook* nb = find_notebook(*child)) return nb;
    }
  }
  return nullptr;
}

std::map<GtkWindow*, int> g_accept_ticks;

// Calls `step` for every visible modal dialog, every 30 ms, until removed.
struct Driver {
  std::function<void(GtkWindow*)> step;
  guint id = 0;
  void start() {
    id = g_timeout_add(
        30,
        +[](gpointer data) -> gboolean {
          auto* self = static_cast<Driver*>(data);
          GList* tops = gtk_window_list_toplevels();
          std::vector<GtkWindow*> dialogs;
          for (GList* it = tops; it != nullptr; it = it->next) {
            GtkWindow* w = GTK_WINDOW(it->data);
            if (GTK_IS_DIALOG(w) && gtk_widget_get_visible(GTK_WIDGET(w)) && gtk_window_get_modal(w)) {
              dialogs.push_back(w);
            }
          }
          g_list_free(tops);
          for (GtkWindow* w : dialogs) self->step(w);
          return G_SOURCE_CONTINUE;
        },
        this);
  }
  void stop() {
    if (id != 0) g_source_remove(id);
    id = 0;
    g_accept_ticks.clear();
  }
  ~Driver() { stop(); }
};
std::string title_of(GtkWindow* w) {
  const char* t = gtk_window_get_title(w);
  return t != nullptr ? t : "";
}
std::string message_of(GtkWindow* w) {
  if (!GTK_IS_MESSAGE_DIALOG(w)) return "";
  gchar* text = nullptr;
  g_object_get(G_OBJECT(w), "text", &text, nullptr);
  std::string out = text != nullptr ? text : "";
  g_free(text);
  return out;
}
std::string filter_name(GtkWindow* w) {
  GtkFileFilter* f = gtk_file_chooser_get_filter(GTK_FILE_CHOOSER(w));
  const char* n = f != nullptr ? gtk_file_filter_get_name(f) : nullptr;
  return n != nullptr ? n : "";
}
// The file chooser checks the name asynchronously and swallows an Accept
// that comes before its folder has loaded, so name it on the first tick and
// keep pressing Save on later ticks until it closes.
void accept_save_as(GtkWindow* w, const std::string& dir, const std::string& name) {
  int& ticks = g_accept_ticks[w];
  if (ticks == 0) {
    gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER(w), dir.c_str());
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(w), name.c_str());
  } else if (ticks >= 5) {
    gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_ACCEPT);
  }
  ++ticks;
}

// A 1x1 single-frame GIF, green.
const unsigned char kGif[] = {
    'G', 'I', 'F', '8', '9', 'a', 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
    0x00, 0xff, 0x00, 0x00, 0x00, 0x00,
    0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00,
    0x02, 0x02, 0x44, 0x01, 0x00, 0x3b};
}  // namespace

int main(int argc, char** argv) {
  const std::string root = temp_dir("lp-0952ui-");
  if (root.empty()) return 1;
  g_setenv("XDG_DATA_HOME", (root + "/data").c_str(), TRUE);
  g_setenv("XDG_CONFIG_HOME", (root + "/config").c_str(), TRUE);
  g_setenv("XDG_STATE_HOME", (root + "/state").c_str(), TRUE);
  g_setenv("XDG_CACHE_HOME", (root + "/cache").c_str(), TRUE);
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_review_095_2_ui: display required but gtk_init_check failed\n");
      return 1;
    }
    std::printf("test_review_095_2_ui: no display, skipping\n");
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();

  const std::string red = root + "/red.png";
  const std::string blue = root + "/blue.png";
  const std::string gif = root + "/anim.gif";
  const std::string big = root + "/wide.png";
  expect(write_png(red, 16, 12, 255, 0, 0), "red fixture");
  expect(write_png(blue, 20, 10, 0, 0, 255), "blue fixture");
  expect(write_png(big, 8300, 2, 10, 20, 30), "8300x2 fixture");
  {
    std::ofstream out(gif, std::ios::binary);
    out.write(reinterpret_cast<const char*>(kGif), sizeof(kGif));
  }

  // ---- Fix 1: tab bar ----
  {
    MainWindow window;
    window.show();
    pump_until([] { return false; }, 200);
    Gtk::Notebook* nb = find_notebook(window);
    expect(nb != nullptr, "tab bar notebook found");
    if (nb == nullptr) return 1;
    expect(!nb->get_visible(), "one document: no tab bar");
    expect(window.open_path(red), "open red");
    expect(window.open_path(blue), "open blue");
    pump_until([&] { return nb->get_mapped() && nb->get_allocated_height() > 1; }, 2000);
    expect(nb->get_visible() && nb->get_mapped(), "two documents: tab bar shown");
    expect(nb->get_n_pages() == 2, "two tabs");
    expect(nb->get_allocated_height() > 1, "tab bar has height");
    Gtk::Widget* label0 = nb->get_n_pages() > 0 ? nb->get_tab_label(*nb->get_nth_page(0)) : nullptr;
    Gtk::Widget* label1 = nb->get_n_pages() > 1 ? nb->get_tab_label(*nb->get_nth_page(1)) : nullptr;
    pump_until([&] { return label0 != nullptr && label0->get_mapped(); }, 2000);
    expect(label0 != nullptr && label0->get_mapped(), "first tab is drawn");
    expect(label1 != nullptr && label1->get_mapped(), "second tab is drawn");
    expect(nb->get_current_page() == 1, "newest document is the current tab");
    expect(window.document().path() == blue, "blue is active");
    nb->set_current_page(0);
    pump_until([] { return false; }, 100);
    expect(nb->get_current_page() == 0, "clicking the first tab selects it");
    expect(window.document().path() == red, "switching tabs shows red");
    nb->set_current_page(1);
    pump_until([] { return false; }, 100);
    expect(window.document().path() == blue, "switching back shows blue");
    window.new_document(30, 30, Color::white());  // what Ctrl+N's OK runs
    pump_until([] { return false; }, 100);
    expect(nb->get_n_pages() == 3 && nb->get_current_page() == 2, "Ctrl+N adds a third tab");
    window.action_close_tab();
    window.action_close_tab();
    pump_until([] { return false; }, 100);
    expect(nb->get_n_pages() == 1 || !nb->get_visible(), "closing back to one document");
    expect(!nb->get_visible(), "one document again: tab bar hidden");
    expect(window.document().path() == red, "the remaining document is red");
    window.hide();
    pump_until([] { return false; }, 100);
  }

  // ---- Fix 2a: outside open while Save As is up is held ----
  {
    MainWindow window;
    window.show();
    pump_until([] { return false; }, 200);
    expect(window.open_path(red), "fix2a: open red");
    bool seen = false;
    bool held = false;
    std::size_t queued = 0;
    Driver d;
    d.step = [&](GtkWindow* w) {
      if (title_of(w) != "Save Image") return;
      if (!seen) {
        seen = true;
        window.request_open(blue);  // what Application::on_open now does
        held = window.document().path() == red;
        queued = window.deferred_open_count();
      }
      accept_save_as(w, root, "held-out.png");
    };
    d.start();
    window.action_save_as();
    d.stop();
    expect(seen, "fix2a: Save Image dialog opened");
    expect(held, "fix2a: the active document did not change under the dialog");
    expect(queued == 1, "fix2a: the outside open was queued");
    int r, g, b;
    first_pixel(root + "/held-out.png", r, g, b);
    expect(r == 255 && g == 0 && b == 0, "fix2a: Save As wrote the red picture");
    expect(window.document().path() == root + "/held-out.png", "fix2a: red now has the new name");
    pump_until([&] { return window.deferred_open_count() == 0 && window.document().path() == blue; }, 3000);
    expect(window.document().path() == blue, "fix2a: the held image opened after the dialog closed");
    first_pixel(red, r, g, b);
    expect(r == 255 && b == 0, "fix2a: the original red file is untouched");
    window.hide();
    pump_until([] { return false; }, 100);
  }

  // ---- Fix 2b: Save As stays bound even if the active tab is switched ----
  {
    MainWindow window;
    window.show();
    pump_until([] { return false; }, 200);
    expect(window.open_path(red), "fix2b: open red");
    bool seen = false;
    bool switched = false;
    Driver d;
    d.step = [&](GtkWindow* w) {
      if (title_of(w) != "Save Image") return;
      if (!seen) {
        seen = true;
        window.open_path(blue);  // bypasses the queue on purpose
        switched = window.document().path() == blue;
      }
      accept_save_as(w, root, "bound-out.png");
    };
    d.start();
    window.action_save_as();
    d.stop();
    expect(seen && switched, "fix2b: something switched tabs during Save As");
    int r, g, b;
    first_pixel(root + "/bound-out.png", r, g, b);
    expect(r == 255 && g == 0 && b == 0, "fix2b: Save As still wrote the red picture");
    LoadedImage out = load_flat_image(root + "/bound-out.png");
    expect(out.width == 16 && out.height == 12, "fix2b: with red's size");
    first_pixel(blue, r, g, b);
    expect(b == 255 && r == 0, "fix2b: blue.png is untouched");
    window.hide();
    pump_until([] { return false; }, 100);
  }

  // ---- Fix 3: GIF via Ctrl+S and via the close prompt ----
  {
    MainWindow window;
    window.show();
    pump_until([] { return false; }, 200);
    expect(window.open_path(gif), "fix3: open GIF");
    window.document().set_dirty(true);
    bool chooser = false;
    std::string name;
    std::string filter;
    bool refused = false;
    Driver d;
    d.step = [&](GtkWindow* w) {
      if (message_of(w).find("GIF save is not supported") != std::string::npos) {
        refused = true;
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_OK);
        return;
      }
      if (title_of(w) == "Save Image" && !chooser) {
        chooser = true;
        gchar* n = gtk_file_chooser_get_current_name(GTK_FILE_CHOOSER(w));
        name = n != nullptr ? n : "";
        g_free(n);
        filter = filter_name(w);
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_CANCEL);
      }
    };
    d.start();
    window.action_save();
    d.stop();
    expect(!refused, "fix3: Ctrl+S on a GIF does not show the GIF refusal");
    expect(chooser, "fix3: Ctrl+S on a GIF opens Save As");
    expect(name == "anim.png", "fix3: Save As proposes anim.png");
    expect(filter.find("PNG") != std::string::npos, "fix3: with the PNG filter");
    expect(window.document().dirty() && window.document().path() == gif,
           "fix3: cancelling Save As keeps the GIF document and its changes");

    // Close prompt: Save, then cancel Save As -> the window must stay.
    int prompts = 0;
    int choosers = 0;
    std::set<GtkWindow*> answered;
    d.step = [&](GtkWindow* w) {
      const std::string msg = message_of(w);
      if (msg.find("GIF save is not supported") != std::string::npos) {
        refused = true;
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_OK);
      } else if (msg.rfind("Save changes", 0) == 0) {
        // run() leaves an answered dialog on screen until its scope ends,
        // so count each prompt once.
        if (answered.insert(w).second) {
          ++prompts;
          gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_YES);
        }
      } else if (title_of(w) == "Save Image") {
        ++choosers;
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_CANCEL);
      }
    };
    d.start();
    const bool closed_after_cancel = window.confirm_close();
    d.stop();
    expect(prompts == 1 && choosers == 1, "fix3: close prompt Save opens Save As");
    expect(!closed_after_cancel, "fix3: cancelling that Save As keeps the window open");
    expect(window.document().dirty(), "fix3: and keeps the changes");
    expect(!refused, "fix3: close prompt does not show the GIF refusal");

    // Close prompt: Save, then accept Save As -> PNG written, close allowed.
    d.step = [&](GtkWindow* w) {
      const std::string msg = message_of(w);
      if (msg.rfind("Save changes", 0) == 0) {
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_YES);
      } else if (title_of(w) == "Save Image") {
        static std::string proposed;
        if (g_accept_ticks[w] == 0) {
          gchar* n = gtk_file_chooser_get_current_name(GTK_FILE_CHOOSER(w));
          proposed = n != nullptr ? n : "";
          g_free(n);
        }
        accept_save_as(w, root, proposed);
      } else if (GTK_IS_MESSAGE_DIALOG(w)) {
        gtk_dialog_response(GTK_DIALOG(w), GTK_RESPONSE_OK);
      }
    };
    d.start();
    const bool closed_after_save = window.confirm_close();
    d.stop();
    expect(closed_after_save, "fix3: close prompt closes after a successful save");
    int r, g, b;
    first_pixel(root + "/anim.png", r, g, b);
    expect(r == 0 && g == 255 && b == 0, "fix3: anim.png has the GIF's picture");
    expect(!window.document().dirty() && window.document().path() == root + "/anim.png",
           "fix3: the document is clean and named anim.png");
    LoadedImage original = load_flat_image(gif);
    expect(original.ok(), "fix3: the original GIF is untouched");
    window.hide();
    pump_until([] { return false; }, 100);
  }

  // ---- Fix 4: large-image warning before the decode ----
  {
    MainWindow window;
    window.show();
    pump_until([] { return false; }, 200);
    const int start = flat_image_decode_count();
    int warnings = 0;
    int decodes_at_warning = -1;
    int answer = GTK_RESPONSE_CANCEL;
    Driver d;
    d.step = [&](GtkWindow* w) {
      if (message_of(w).find("larger than 8192") != std::string::npos) {
        ++warnings;
        decodes_at_warning = flat_image_decode_count();
        gtk_dialog_response(GTK_DIALOG(w), answer);
      }
    };
    d.start();
    const bool opened_cancel = window.open_path(big);
    d.stop();
    expect(warnings == 1, "fix4: one warning");
    expect(decodes_at_warning == start, "fix4: the warning came before any decode");
    expect(!opened_cancel, "fix4: Cancel opens nothing");
    expect(flat_image_decode_count() == start, "fix4: Cancel decoded nothing");
    expect(window.document().path().empty(), "fix4: the blank canvas stays");

    warnings = 0;
    answer = GTK_RESPONSE_OK;
    d.start();
    const bool opened_ok = window.open_path(big);
    d.stop();
    expect(opened_ok, "fix4: OK opens the image");
    expect(warnings == 1, "fix4: still only one warning (no second after the decode)");
    expect(flat_image_decode_count() == start + 1, "fix4: OK decodes once");
    expect(window.document().width() == 8300, "fix4: full size");
    window.hide();
    pump_until([] { return false; }, 100);
  }

  const std::string cmd = "rm -rf '" + root + "'";
  if (std::system(cmd.c_str()) != 0) std::printf("cleanup failed %s\n", root.c_str());
  std::printf("%d failure(s)\n", errors);
  return errors == 0 ? 0 : 1;
}
