// SPDX-License-Identifier: GPL-3.0-or-later
// Timings for the viewport expose path, the Normal blend, and zoomed-out
// OpenRaster group compositing. Prints one "key_ms value" line per measurement.

#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
#include "io/ora.hpp"
#include "raster/blend.hpp"
#include "raster/types.hpp"
#include "ui/canvas_view.hpp"

#include <cairo.h>
#include <gdk/gdk.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include <glibmm/main.h>
#include <gtkmm/main.h>
#include <gtkmm/window.h>

namespace {

using clock = std::chrono::steady_clock;

double ms_since(clock::time_point start) {
  return std::chrono::duration<double, std::milli>(clock::now() - start).count();
}

void report(const char* key, double ms) {
  std::printf("%s %.4f\n", key, ms);
  std::fflush(stdout);
}

void premultiply_rows(std::uint8_t* pixels, int width, int height, int stride) {
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      const int a = p[3];
      p[0] = static_cast<std::uint8_t>((static_cast<int>(p[2]) * a + 127) / 255);
      p[1] = static_cast<std::uint8_t>((static_cast<int>(p[1]) * a + 127) / 255);
      p[2] = static_cast<std::uint8_t>((static_cast<int>(p[0]) * a + 127) / 255);
      p[3] = static_cast<std::uint8_t>(a);
    }
  }
}

void fill_layer(lundukepaint::Layer& layer, lundukepaint::Color color, int alpha_pattern) {
  std::vector<std::uint8_t> px(static_cast<std::size_t>(layer.stride()) *
                                   static_cast<std::size_t>(layer.height()),
                               0);
  for (int y = 0; y < layer.height(); ++y) {
    std::uint8_t* row = px.data() + static_cast<std::size_t>(y) * layer.stride();
    for (int x = 0; x < layer.width(); ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      p[0] = color.r;
      p[1] = color.g;
      p[2] = color.b;
      if (alpha_pattern == 0) {
        p[3] = 255;
      } else if (alpha_pattern == 1) {
        p[3] = static_cast<std::uint8_t>(40 + ((x * 17 + y * 9) & 127));
      } else {
        p[3] = ((x + y) & 7) == 0 ? 0 : 255;
      }
    }
  }
  layer.set_pixels(layer.width(), layer.height(), px.data(), layer.stride());
}

void bench_blend() {
  constexpr int kW = 1280;
  constexpr int kH = 800;
  auto doc = lundukepaint::Document::create(kW, kH, lundukepaint::Color::white());
  fill_layer(doc->layers().active_layer(), {180, 40, 20, 255}, 0);
  const lundukepaint::Color extras[3] = {{20, 80, 200, 255}, {20, 160, 40, 255}, {220, 180, 20, 255}};
  for (int i = 0; i < 3; ++i) {
    auto layer = std::make_unique<lundukepaint::Layer>(kW, kH, lundukepaint::Color::transparent(), "L");
    fill_layer(*layer, extras[i], 1);
    layer->set_opacity(0.65f);
    doc->layers().insert(doc->layers().count(), std::move(layer));
  }
  std::vector<std::uint8_t> dest(static_cast<std::size_t>(kW) * kH * 4, 0);
  const lundukepaint::Rect view{0, 0, kW, kH};
  doc->layers().composite_rect(dest.data(), kW * 4, view);
  constexpr int kIters = 4;
  const auto start = clock::now();
  for (int i = 0; i < kIters; ++i) {
    doc->layers().composite_rect(dest.data(), kW * 4, view);
  }
  report("blend_composite_1280x800_4layer_ms", ms_since(start) / kIters);

  std::vector<std::uint8_t> copy = dest;
  const auto pm = clock::now();
  constexpr int kPm = 8;
  for (int i = 0; i < kPm; ++i) {
    std::memcpy(copy.data(), dest.data(), dest.size());
    premultiply_rows(copy.data(), kW, kH, kW * 4);
  }
  report("premultiply_1280x800_ms", ms_since(pm) / kPm);
}

void bench_ora() {
  constexpr int kW = 1600;
  constexpr int kH = 1200;
  constexpr int kOutW = 480;
  constexpr int kOutH = 360;
  auto doc = lundukepaint::Document::create(kW, kH, lundukepaint::Color{30, 30, 30, 255});
  fill_layer(doc->layers().active_layer(), {200, 40, 40, 255}, 2);
  auto top = std::make_unique<lundukepaint::Layer>(kW, kH, lundukepaint::Color::transparent(), "Top");
  fill_layer(*top, {40, 80, 220, 255}, 1);
  top->set_opacity(0.8f);
  doc->layers().insert(1, std::move(top));

  lundukepaint::OraNode root;
  root.is_stack = true;
  root.name = "root";
  lundukepaint::OraNode group;
  group.is_stack = true;
  group.name = "group";
  group.opacity = 0.55f;
  lundukepaint::OraNode leaf;
  leaf.layer_index = 1;
  group.children.push_back(leaf);
  lundukepaint::OraNode back;
  back.layer_index = 0;
  root.children.push_back(group);
  root.children.push_back(back);
  doc->set_ora_stack(std::move(root));

  std::vector<std::uint8_t> out(static_cast<std::size_t>(kOutW) * kOutH * 4, 0);
  lundukepaint::LayerStack::CompositeWork work;
  const lundukepaint::Rect view{0, 0, kW, kH};
  doc->layers().composite_scaled(out.data(), kOutW * 4, view, kOutW, kOutH, nullptr, -1, -1, {}, &work);
  constexpr int kIters = 3;
  work.samples = 0;
  const auto start = clock::now();
  for (int i = 0; i < kIters; ++i) {
    doc->layers().composite_scaled(out.data(), kOutW * 4, view, kOutW, kOutH, nullptr, -1, -1, {}, &work);
  }
  report("ora_zoom_480x360_from_1600x1200_ms", ms_since(start) / kIters);
  std::printf("ora_zoom_samples %d\n", work.samples);
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

struct DrawStats {
  int widgets = 0;
  int skipped = 0;
  unsigned checksum = 0;
  bool sample = false;
};

void draw_tree(GtkWidget* widget, DrawStats& stats) {
  if (widget == nullptr || !gtk_widget_get_visible(widget)) {
    return;
  }
  GtkAllocation alloc;
  gtk_widget_get_allocation(widget, &alloc);
  GdkWindow* window = gtk_widget_get_window(widget);
  if (!gtk_widget_get_realized(widget) || alloc.width < 2 || alloc.height < 2 || window == nullptr) {
    ++stats.skipped;
  } else if (gtk_widget_get_has_window(widget)) {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, alloc.width, alloc.height);
    cairo_t* cr = cairo_create(surface);
    gtk_widget_draw(widget, cr);
    cairo_destroy(cr);
    if (stats.sample) {
      cairo_surface_flush(surface);
      const unsigned char* pixels = cairo_image_surface_get_data(surface);
      const int stride = cairo_image_surface_get_stride(surface);
      unsigned sum = 0;
      if (pixels != nullptr) {
        for (int y = 0; y < alloc.height; y += 8) {
          const unsigned char* row = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
          for (int x = 0; x < alloc.width; x += 8) {
            sum += row[x * 4];
          }
        }
      }
      stats.checksum += sum;
    }
    ++stats.widgets;
    cairo_surface_destroy(surface);
  }
  if (GTK_IS_CONTAINER(widget)) {
    gtk_container_foreach(
        GTK_CONTAINER(widget),
        [](GtkWidget* child, gpointer data) { draw_tree(child, *static_cast<DrawStats*>(data)); }, &stats);
  }
}

void bench_expose() {
  if (gdk_display_get_default() == nullptr) {
    std::printf("expose_skipped 1\n");
    return;
  }
  constexpr int kW = 960;
  constexpr int kH = 600;
  auto doc = lundukepaint::Document::create(kW, kH, lundukepaint::Color::white());
  fill_layer(doc->layers().active_layer(), {20, 90, 160, 255}, 0);
  auto over = std::make_unique<lundukepaint::Layer>(kW, kH, lundukepaint::Color::transparent(), "Over");
  fill_layer(*over, {220, 80, 40, 255}, 1);
  over->set_opacity(0.7f);
  doc->layers().insert(1, std::move(over));
  doc->selection().set_rect(lundukepaint::Rect{80, 60, 400, 280});

  Gtk::Window window;
  lundukepaint::CanvasView view;
  window.add(view);
  window.set_default_size(1100, 700);
  window.show_all();
  view.set_document(doc.get());
  Gtk::Allocation top{0, 0, 1100, 700};
  window.size_allocate(top);
  pump(200);
  GtkAllocation alloc{};
  gtk_widget_get_allocation(GTK_WIDGET(view.gobj()), &alloc);
  std::printf("expose_alloc %d %d\n", alloc.width, alloc.height);
  DrawStats once;
  once.sample = true;
  draw_tree(GTK_WIDGET(view.gobj()), once);
  std::printf("expose_widgets %d skipped %d checksum %u\n", once.widgets, once.skipped, once.checksum);

  constexpr int kFrames = 8;
  const auto start = clock::now();
  for (int i = 0; i < kFrames; ++i) {
    DrawStats frame;
    draw_tree(GTK_WIDGET(view.gobj()), frame);
  }
  report("expose_warm_selection_ms", ms_since(start) / kFrames);

  const auto cold = clock::now();
  constexpr int kCold = 3;
  for (int i = 0; i < kCold; ++i) {
    view.invalidate_all();
    DrawStats frame;
    draw_tree(GTK_WIDGET(view.gobj()), frame);
  }
  report("expose_cold_invalidate_ms", ms_since(cold) / kCold);
}

}  // namespace

int main(int argc, char** argv) {
  Gtk::Main kit(argc, argv);
  bench_blend();
  bench_ora();
  bench_expose();
  return 0;
}
