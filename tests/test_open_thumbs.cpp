// SPDX-License-Identifier: GPL-3.0-or-later
// Open dialog thumbnail grid: a folder of images gets thumbs off the UI thread,
// and the chooser's side preview widget is still installed.

#include "ui/open_thumbs.hpp"

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gdkmm/pixbuf.h>
#include <glib.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/image.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <sys/stat.h>
#include <unistd.h>

namespace {

void pump(int ms) {
  const gint64 end = g_get_monotonic_time() + static_cast<gint64>(ms) * 1000;
  do {
    while (gtk_events_pending()) {
      gtk_main_iteration_do(FALSE);
    }
    g_usleep(2000);
  } while (g_get_monotonic_time() < end);
}

int count_pixbufs(GtkWidget* widget) {
  int n = 0;
  if (GTK_IS_IMAGE(widget)) {
    if (gtk_image_get_pixbuf(GTK_IMAGE(widget)) != nullptr) {
      ++n;
    }
  }
  if (GTK_IS_CONTAINER(widget)) {
    gtk_container_foreach(
        GTK_CONTAINER(widget),
        [](GtkWidget* child, gpointer data) {
          *static_cast<int*>(data) += count_pixbufs(child);
        },
        &n);
  }
  return n;
}

}  // namespace

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_open_thumbs: display required\n");
      return 1;
    }
    std::printf("test_open_thumbs: no display, skipping\n");
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();

  char dir[] = "/tmp/lunduke-thumbs-XXXXXX";
  if (mkdtemp(dir) == nullptr) {
    std::fprintf(stderr, "test_open_thumbs: mkdtemp failed\n");
    return 1;
  }
  const std::string folder(dir);
  const std::string png = folder + "/swatch.png";
  auto pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, 8, 8);
  pixbuf->fill(0xff2200ff);
  GError* error = nullptr;
  if (!gdk_pixbuf_save(pixbuf->gobj(), png.c_str(), "png", &error, nullptr)) {
    std::fprintf(stderr, "test_open_thumbs: save failed\n");
    return 1;
  }

  Gtk::Window parent;
  parent.set_default_size(200, 100);
  parent.show_all();
  Gtk::FileChooserDialog dialog(parent, "Open Image", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Open", Gtk::RESPONSE_ACCEPT);
  Gtk::Image preview;
  dialog.set_preview_widget(preview);
  dialog.set_preview_widget_active(false);
  lundukepaint::OpenThumbGrid thumbs(dialog);
  dialog.set_extra_widget(thumbs);
  dialog.set_current_folder(folder);
  dialog.show_all();
  pump(1500);

  const int found = count_pixbufs(GTK_WIDGET(thumbs.gobj()));
  const bool preview_kept = dialog.get_preview_widget() == &preview;
  dialog.hide();
  pump(50);

  if (!preview_kept || found < 1) {
    std::fprintf(stderr, "test_open_thumbs: preview_kept=%d thumbs=%d\n", preview_kept ? 1 : 0,
                 found);
    return 1;
  }
  unlink(png.c_str());
  rmdir(dir);
  std::printf("test_open_thumbs: ok\n");
  return 0;
}
