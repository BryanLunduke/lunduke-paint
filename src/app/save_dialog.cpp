// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/save_dialog.hpp"

#include <memory>

namespace lundukepaint {

SaveFilters add_save_filters(Gtk::FileChooser& chooser) {
  SaveFilters filters;
  filters.ora = Gtk::FileFilter::create();
  filters.ora->set_name("OpenRaster project (*.ora)");
  filters.ora->add_pattern("*.ora");
  filters.png = Gtk::FileFilter::create();
  filters.png->set_name("PNG image (*.png)");
  filters.png->add_pattern("*.png");
  filters.jpeg = Gtk::FileFilter::create();
  filters.jpeg->set_name("JPEG image (*.jpg)");
  filters.jpeg->add_pattern("*.jpg");
  filters.jpeg->add_pattern("*.jpeg");
  filters.bmp = Gtk::FileFilter::create();
  filters.bmp->set_name("BMP image (*.bmp)");
  filters.bmp->add_pattern("*.bmp");
  chooser.add_filter(filters.ora);
  chooser.add_filter(filters.png);
  chooser.add_filter(filters.jpeg);
  chooser.add_filter(filters.bmp);
  return filters;
}

ImageFormat format_of_save_filter(const Glib::RefPtr<Gtk::FileFilter>& filter, const SaveFilters& filters) {
  if (!filter) {
    return ImageFormat::Unknown;
  }
  if (filter == filters.ora) {
    return ImageFormat::Ora;
  }
  if (filter == filters.png) {
    return ImageFormat::Png;
  }
  if (filter == filters.jpeg) {
    return ImageFormat::Jpeg;
  }
  if (filter == filters.bmp) {
    return ImageFormat::Bmp;
  }
  return ImageFormat::Unknown;
}

void watch_save_filter(Gtk::FileChooser& chooser, const SaveFilters& filters, ImageFormat initial) {
  auto previous = std::make_shared<ImageFormat>(initial);
  auto updating = std::make_shared<bool>(false);
  chooser.property_filter().signal_changed().connect([&chooser, filters, previous, updating]() {
    if (*updating) {
      return;
    }
    const ImageFormat next = format_of_save_filter(chooser.get_filter(), filters);
    if (next == *previous) {
      return;
    }
    std::string cur = chooser.get_current_name();
    if (cur.empty()) {
      const std::string full = chooser.get_filename();
      if (!full.empty()) {
        const auto slash = full.find_last_of("/\\");
        cur = slash == std::string::npos ? full : full.substr(slash + 1);
      }
    }
    const std::string updated = name_for_filter_change(cur, *previous, next);
    *previous = next;
    if (updated.empty() || updated == cur) {
      return;
    }
    *updating = true;
    chooser.set_current_name(updated);
    *updating = false;
  });
}

bool complete_save_choice(const std::string& chosen_path, ImageFormat filter,
                          const std::function<bool(const SaveTarget&)>& accept_append, std::string& path,
                          ImageFormat& format, std::string& error) {
  const SaveTarget target = resolve_save_target(chosen_path, filter);
  if (target.kind == SaveResolveKind::Ready) {
    path = target.path;
    format = target.format;
    error.clear();
    return true;
  }
  if (target.kind == SaveResolveKind::PromptAppend) {
    // The question dialog is the message. Declining it is a cancel, not a
    // second error.
    if (accept_append && accept_append(target)) {
      path = target.offered_path;
      format = target.offered_format;
      error.clear();
      return true;
    }
    error.clear();
    return false;
  }
  error = target.message.empty() ? "Choose a file name." : target.message;
  return false;
}

}  // namespace lundukepaint
