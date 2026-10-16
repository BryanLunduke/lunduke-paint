// SPDX-License-Identifier: GPL-3.0-or-later
//
// LCOS#136: the main window must shrink well below a 1366x768 screen's usable
// height (768 minus an XFCE panel and a title bar) with every tool selected,
// with the right dock shown or hidden, and with a document open. A missing
// display is a skip (exit 77) unless LUNDUKEPAINT_REQUIRE_DISPLAY is set.

#include "app/main_window.hpp"
#include "ui/toolbox_catalog.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <glib.h>
#include <gtk/gtk.h>
#include <gtkmm/main.h>

namespace {

int errors = 0;

void pump(int rounds) {
  for (int i = 0; i < rounds; ++i) {
    while (gtk_events_pending()) {
      gtk_main_iteration_do(FALSE);
    }
    g_usleep(2000);
  }
}

void dump_children(GtkWidget* widget, int depth) {
  if (depth > 3 || !GTK_IS_CONTAINER(widget)) {
    return;
  }
  GList* kids = gtk_container_get_children(GTK_CONTAINER(widget));
  for (GList* l = kids; l != nullptr; l = l->next) {
    GtkWidget* child = GTK_WIDGET(l->data);
    if (!gtk_widget_get_visible(child)) {
      continue;
    }
    int mn = 0;
    int nat = 0;
    gtk_widget_get_preferred_height(child, &mn, &nat);
    std::printf("test_min_height: %*s%s min %d nat %d\n", depth * 2, "", G_OBJECT_TYPE_NAME(child), mn,
                nat);
    dump_children(child, depth + 1);
  }
  g_list_free(kids);
}

int min_height(lundukepaint::MainWindow& window) {
  int mn = 0;
  int nat = 0;
  window.get_preferred_height(mn, nat);
  int w_min = 0;
  int w_nat = 0;
  window.get_preferred_width(w_min, w_nat);
  int hfw_min = 0;
  int hfw_nat = 0;
  window.get_preferred_height_for_width(w_min, hfw_min, hfw_nat);
  return mn > hfw_min ? mn : hfw_min;
}

std::string temp_dir(const char* prefix) {
  char tmpl[64];
  std::snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
  char* made = mkdtemp(tmpl);
  return made == nullptr ? std::string() : std::string(made);
}

}  // namespace

int main(int argc, char** argv) {
  const std::string config_home = temp_dir("lp-mh-cfg-");
  const std::string state_home = temp_dir("lp-mh-st-");
  g_setenv("XDG_CONFIG_HOME", config_home.c_str(), TRUE);
  g_setenv("XDG_STATE_HOME", state_home.c_str(), TRUE);
  if (!gtk_init_check(&argc, &argv)) {
    const char* require = std::getenv("LUNDUKEPAINT_REQUIRE_DISPLAY");
    if (require != nullptr && require[0] != '\0' && std::strcmp(require, "0") != 0) {
      std::fprintf(stderr, "test_min_height: display required but gtk_init_check failed\n");
      return 1;
    }
    return 77;
  }
  Gtk::Main::init_gtkmm_internals();

  // 1366x768 with a 28-36 px XFCE panel and a ~30 px title bar leaves about
  // 700 px. 0.9.4-8 measured 324 on Adwaita; 0.9.4-9 must stay under 300. Ask for a lot less so taller panels/themes (Breeze) still fit.
  constexpr int kCap = 300;

  lundukepaint::MainWindow window;
  window.show();
  pump(60);
  int worst = 0;
  std::string worst_tool;
  int count = 0;
  const lundukepaint::ToolboxTool* catalog = lundukepaint::toolbox_tools(count);
  for (int i = 0; i < count; ++i) {
    window.set_active_tool(catalog[i].id);
    pump(5);
    const int h = min_height(window);
    std::printf("test_min_height: tool %s min height %d\n", catalog[i].id, h);
    if (h > worst) {
      worst = h;
      worst_tool = catalog[i].id;
    }
  }
  window.set_active_tool("pencil");
  pump(5);
  dump_children(GTK_WIDGET(window.gobj()), 0);
  std::printf("test_min_height: worst minimum height %d (%s), cap %d\n", worst, worst_tool.c_str(), kCap);
  if (worst <= 0 || worst > kCap) {
    std::fprintf(stderr, "test_min_height: minimum height %d exceeds %d\n", worst, kCap);
    ++errors;
  }

  // The window really shrinks there (not just reports it).
  window.resize(1000, 200);
  pump(80);
  int gw = 0;
  int gh = 0;
  window.get_size(gw, gh);
  std::printf("test_min_height: resized to 1000x200, got %dx%d\n", gw, gh);
  if (gh > kCap) {
    std::fprintf(stderr, "test_min_height: window stayed %d tall\n", gh);
    ++errors;
  }
  window.hide();
  pump(5);
  return errors == 0 ? 0 : 1;
}
