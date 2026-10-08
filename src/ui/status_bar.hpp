// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_STATUS_BAR_HPP
#define LUNDUKEPAINT_UI_STATUS_BAR_HPP

#include <glibmm/ustring.h>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include <sigc++/connection.h>

namespace lundukepaint {

class StatusBar : public Gtk::Box {
public:
  StatusBar();

  void set_hint(const Glib::ustring& hint);
  void show_coordinates(double x, double y);
  void clear_coordinates();
  void set_canvas_size(int width, int height);
  void set_selection_size(int width, int height, bool visible);
  void set_zoom(double zoom);
  void set_modified(bool modified);
  void show_message(const Glib::ustring& message);

private:
  Gtk::Label hint_;
  Glib::ustring hint_text_;
  bool message_active_{false};
  sigc::connection message_timer_;
  Gtk::Label coords_;
  Gtk::Label sel_;
  Gtk::Label size_;
  Gtk::Label zoom_;
  Gtk::Label modified_;
};

}  // namespace lundukepaint

#endif
