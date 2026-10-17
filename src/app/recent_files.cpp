// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/recent_files.hpp"

#include "io/image_io.hpp"

#include <glibmm/convert.h>
#include <gtkmm/recentmanager.h>

namespace lundukepaint {

std::string recent_mime_for_path(const std::string& path) {
  switch (format_from_path(path)) {
    case ImageFormat::Ora:
      return "image/openraster";
    case ImageFormat::Png:
      return "image/png";
    case ImageFormat::Jpeg:
      return "image/jpeg";
    case ImageFormat::Bmp:
      return "image/bmp";
    case ImageFormat::Gif:
      return "image/gif";
    case ImageFormat::Unknown:
      break;
  }
  return {};
}

bool register_recent_file(const std::string& path) {
  if (path.empty() || path[0] != '/') {
    return false;
  }
  try {
    auto manager = Gtk::RecentManager::get_default();
    if (!manager) {
      return false;
    }
    const Glib::ustring uri = Glib::filename_to_uri(path);
    Gtk::RecentManager::Data data;
    data.app_name = "Lunduke Paint";
    data.app_exec = "lunduke-paint %f";
    data.mime_type = recent_mime_for_path(path);
    if (data.mime_type.empty()) {
      return manager->add_item(uri);
    }
    return manager->add_item(uri, data);
  } catch (const Glib::Error&) {
    return false;
  }
}

}  // namespace lundukepaint
