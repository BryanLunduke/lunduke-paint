// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_SAVE_DIALOG_HPP
#define LUNDUKEPAINT_APP_SAVE_DIALOG_HPP

#include "io/save_target.hpp"

#include <functional>
#include <string>

#include <glibmm/refptr.h>
#include <gtkmm/filechooser.h>
#include <gtkmm/filefilter.h>

namespace lundukepaint {

// The four Save / Save As / export filters. Flat export is one of these,
// not a second dialog.
struct SaveFilters {
  Glib::RefPtr<Gtk::FileFilter> ora;
  Glib::RefPtr<Gtk::FileFilter> png;
  Glib::RefPtr<Gtk::FileFilter> jpeg;
  Glib::RefPtr<Gtk::FileFilter> bmp;
};

SaveFilters add_save_filters(Gtk::FileChooser& chooser);

ImageFormat format_of_save_filter(const Glib::RefPtr<Gtk::FileFilter>& filter, const SaveFilters& filters);

// Keep the name entry in step with the filter. Call after the initial name
// and filter are set. `initial` is the format that currently owns the suffix
// (the selected filter, when the name was filled in to match it).
void watch_save_filter(Gtk::FileChooser& chooser, const SaveFilters& filters, ImageFormat initial);

// Shared accept path for Save As and any export that uses the same chooser.
// PromptAppend asks `accept_append`; cancel leaves path unset and returns false.
bool complete_save_choice(const std::string& chosen_path, ImageFormat filter,
                          const std::function<bool(const SaveTarget&)>& accept_append, std::string& path,
                          ImageFormat& format, std::string& error);

}  // namespace lundukepaint

#endif
