// SPDX-License-Identifier: GPL-3.0-or-later
// Round 15 P-4, on a display: opening an image registers it with the
// desktop's shared recent-files list (Gtk::RecentManager), so the GTK file
// chooser's "Recent" place shows it; register_recent_file refuses relative
// paths. A missing display is a skip (77) unless LUNDUKEPAINT_REQUIRE_DISPLAY.

#include "app/main_window.hpp"
#include "app/recent_files.hpp"
#include "io/image_io.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <vector>

#include <glib.h>
#include <glibmm/convert.h>
#include <gtk/gtk.h>
#include <gtkmm/main.h>
#include <gtkmm/recentmanager.h>

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
std::string slurp(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
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
}  // namespace

int main(int argc, char** argv) {
  const std::string root = temp_dir("lp-r15ui-");
  if (root.empty()) return 1;
  const std::string data_home = root + "/data";
  g_mkdir_with_parents(data_home.c_str(), 0700);
  g_setenv("XDG_DATA_HOME", data_home.c_str(), TRUE);
  g_setenv("XDG_CONFIG_HOME", (root + "/config").c_str(), TRUE);
  g_setenv("XDG_STATE_HOME", (root + "/state").c_str(), TRUE);
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_review_r15_ui: display required but gtk_init_check failed\n");
      return 1;
    }
    std::printf("test_review_r15_ui: no display, skipping\n");
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();
  // GTK only records recent files when this setting is on (the default).
  g_object_set(gtk_settings_get_default(), "gtk-recent-files-enabled", TRUE, nullptr);

  expect(!lundukepaint::register_recent_file("relative.png"), "relative path refused");
  expect(lundukepaint::recent_mime_for_path("/a/b.ora") == "image/openraster", "ora mime");
  expect(lundukepaint::recent_mime_for_path("/a/b.JPG") == "image/jpeg", "jpeg mime");

  const std::string png = root + "/r15 recent test.png";
  std::vector<std::uint8_t> px(8 * 8 * 4, 128);
  std::string err;
  expect(lundukepaint::save_flat_image(png, lundukepaint::ImageFormat::Png, px.data(), 8, 8, 32, 90, err),
         "fixture PNG written");
  const std::string uri = Glib::filename_to_uri(png);
  const std::string xbel = data_home + "/recently-used.xbel";

  {
    lundukepaint::MainWindow window;
    window.show();
    pump_until([] { return false; }, 200);
    expect(window.open_path(png), "MainWindow opens the PNG");
    const bool written = pump_until(
        [&] { return slurp(xbel).find("r15%20recent%20test.png") != std::string::npos; }, 8000);
    expect(written, "recently-used.xbel lists the opened image");
    const std::string text = slurp(xbel);
    expect(text.find("Lunduke Paint") != std::string::npos, "entry names Lunduke Paint");
    expect(text.find("image/png") != std::string::npos, "entry has image/png");
    auto manager = Gtk::RecentManager::get_default();
    expect(manager && manager->has_item(uri), "Gtk::RecentManager has the item");
    window.hide();
    pump_until([] { return false; }, 100);
  }

  const std::string cmd = "rm -rf '" + root + "'";
  if (std::system(cmd.c_str()) != 0) std::printf("cleanup failed %s\n", root.c_str());
  std::printf("%d failure(s)\n", errors);
  return errors == 0 ? 0 : 1;
}
