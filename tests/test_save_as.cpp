// SPDX-License-Identifier: GPL-3.0-or-later
// Drives the real Save As chooser: filter OpenRaster, typed name bft.png.
// The file that gets written must be a PNG, and bft.ora must not exist.

#include "app/save_dialog.hpp"
#include "io/image_io.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <glib.h>
#include <glibmm/main.h>
#include <gtk/gtk.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>

#include <sys/stat.h>
#include <unistd.h>

namespace {

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_save_as: %s\n", msg);
    ++errors;
  }
}

std::string make_temp_dir() {
  char tmpl[] = "/tmp/lunduke-save-as-XXXXXX";
  char* dir = mkdtemp(tmpl);
  return dir == nullptr ? std::string() : std::string(dir);
}

bool read_prefix(const std::string& path, unsigned char* out, std::size_t n) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  in.read(reinterpret_cast<char*>(out), static_cast<std::streamsize>(n));
  return in.gcount() == static_cast<std::streamsize>(n);
}

}  // namespace

int main(int argc, char** argv) {
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_save_as: display required (LUNDUKEPAINT_REQUIRE_DISPLAY) but gtk_init_check failed\n");
      return 1;
    }
    std::printf("test_save_as: no display, skipping\n");
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();

  const std::string dir = make_temp_dir();
  expect(!dir.empty(), "temp directory");
  if (dir.empty()) {
    return 1;
  }

  Gtk::Window window;
  window.set_default_size(640, 400);
  window.set_title("Save As test");
  window.show();

  Gtk::FileChooserDialog dialog(window, "Save Image", Gtk::FILE_CHOOSER_ACTION_SAVE);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Save", Gtk::RESPONSE_ACCEPT);
  dialog.set_do_overwrite_confirmation(true);
  dialog.set_default_size(620, 400);
  const lundukepaint::SaveFilters filters = lundukepaint::add_save_filters(dialog);
  expect(static_cast<bool>(filters.ora), "OpenRaster filter exists");
  expect(filters.ora->get_name() == "OpenRaster project (*.ora)", "OpenRaster filter name");
  dialog.set_current_folder(dir);
  dialog.set_current_name("untitled.ora");
  dialog.set_filter(filters.ora);
  lundukepaint::watch_save_filter(dialog, filters, lundukepaint::ImageFormat::Ora);

  bool armed = false;
  dialog.signal_map().connect([&]() {
    if (armed) {
      return;
    }
    armed = true;
    dialog.set_current_folder(dir);
    dialog.set_filter(filters.ora);
    dialog.set_current_name("bft.png");
    Glib::signal_idle().connect([&]() {
      dialog.response(Gtk::RESPONSE_ACCEPT);
      return false;
    });
  });

  const int response = dialog.run();
  expect(response == Gtk::RESPONSE_ACCEPT, "Save As accepted");
  expect(dialog.get_current_name() == "bft.png", "the name entry is still bft.png");
  const lundukepaint::ImageFormat filter =
      lundukepaint::format_of_save_filter(dialog.get_filter(), filters);
  expect(filter == lundukepaint::ImageFormat::Ora, "the filter is still OpenRaster");

  std::string chosen = dialog.get_filename();
  if (chosen.empty()) {
    chosen = dir + "/bft.png";
  }
  std::fprintf(stderr, "test_save_as: chooser filename '%s'\n", chosen.c_str());

  bool prompted = false;
  std::string path;
  lundukepaint::ImageFormat format = lundukepaint::ImageFormat::Unknown;
  std::string error;
  const bool ok = lundukepaint::complete_save_choice(
      chosen, filter,
      [&](const lundukepaint::SaveTarget&) {
        prompted = true;
        return false;
      },
      path, format, error);
  expect(ok, "bft.png resolves");
  expect(!prompted, "a known extension does not ask");
  expect(error.empty(), "no error for bft.png");
  expect(format == lundukepaint::ImageFormat::Png, "resolved format is PNG");
  expect(path.size() >= 7 && path.compare(path.size() - 7, 7, "bft.png") == 0, "resolved path ends with bft.png");
  expect(path.find("bft.ora") == std::string::npos, "resolved path is not bft.ora");

  if (ok && format == lundukepaint::ImageFormat::Png) {
    std::vector<std::uint8_t> rgba(2 * 2 * 4, 0);
    rgba[0] = 200;
    rgba[3] = 255;
    std::string write_error;
    expect(lundukepaint::save_flat_image(path, format, rgba.data(), 2, 2, 8, 90, write_error),
           "write the resolved PNG");
    if (!write_error.empty()) {
      std::fprintf(stderr, "test_save_as: write error %s\n", write_error.c_str());
    }
    unsigned char magic[8] = {};
    expect(read_prefix(path, magic, 8), "read magic");
    const unsigned char png[8] = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
    expect(std::memcmp(magic, png, 8) == 0, "magic bytes are PNG");
  }

  const std::string ora = dir + "/bft.ora";
  struct stat st;
  expect(stat(ora.c_str(), &st) != 0 && errno == ENOENT, "bft.ora was not created");

  dialog.hide();
  window.hide();
  if (!path.empty()) {
    ::unlink(path.c_str());
  }
  ::rmdir(dir.c_str());

  if (errors != 0) {
    std::fprintf(stderr, "test_save_as: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_save_as: ok\n");
  return 0;
}
