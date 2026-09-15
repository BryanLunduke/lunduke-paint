// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_SYMBOLIC_ICON_HPP
#define LUNDUKEPAINT_UI_SYMBOLIC_ICON_HPP

#include <gdkmm/pixbuf.h>
#include <giomm/memoryinputstream.h>
#include <glibmm/bytes.h>
#include <gtkmm/stylecontext.h>
#include <gtkmm/widget.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace lundukepaint {
namespace symbolic_icon {

// Resolve the theme foreground to #rrggbb. Pixbuf SVG loading bakes fills at
// decode time, so toolbox icons cannot rely on CSS currentColor alone.
inline std::string theme_fg_hex(Gtk::Widget& host) {
  Gdk::RGBA fg;
  auto ctx = host.get_style_context();
  if (!ctx->lookup_color("theme_fg_color", fg) && !ctx->lookup_color("fg_color", fg)) {
    fg.set_rgba(0.2, 0.2, 0.2, 1.0);
  }
  char hex[8];
  std::snprintf(hex, sizeof(hex), "#%02x%02x%02x",
                static_cast<int>(fg.get_red() * 255.0 + 0.5),
                static_cast<int>(fg.get_green() * 255.0 + 0.5),
                static_cast<int>(fg.get_blue() * 255.0 + 0.5));
  return hex;
}

inline void replace_all(std::string& haystack, const std::string& from, const std::string& to) {
  if (from.empty()) {
    return;
  }
  std::size_t pos = 0;
  while ((pos = haystack.find(from, pos)) != std::string::npos) {
    haystack.replace(pos, from.size(), to);
    pos += to.size();
  }
}

// Load a symbolic SVG from a GResource, substituting currentColor (and any
// leftover #bebebe) with theme_fg_color so icons look enabled on Clearlooks
// and other themes — not washed grey.
inline Glib::RefPtr<Gdk::Pixbuf> load_from_resource(Gtk::Widget& host,
                                                    const std::string& resource_path,
                                                    int pixel_size) {
  const std::string hex = theme_fg_hex(host);

  GError* error = nullptr;
  GBytes* bytes =
      g_resources_lookup_data(resource_path.c_str(), G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
  if (bytes == nullptr) {
    if (error != nullptr) {
      Glib::Error::throw_exception(error);
    }
    throw std::runtime_error("resource missing: " + resource_path);
  }

  gsize data_size = 0;
  const char* data = static_cast<const char*>(g_bytes_get_data(bytes, &data_size));
  std::string svg(data, data_size);
  g_bytes_unref(bytes);

  replace_all(svg, "currentColor", hex);
  replace_all(svg, "#bebebe", hex);
  replace_all(svg, "#BEBEBE", hex);

  auto stream = Gio::MemoryInputStream::create();
  stream->add_data(svg);
  return Gdk::Pixbuf::create_from_stream_at_scale(stream, pixel_size, pixel_size, true);
}

}  // namespace symbolic_icon
}  // namespace lundukepaint

#endif
