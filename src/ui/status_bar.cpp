// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/status_bar.hpp"

#include <glibmm/main.h>

#include <cstdio>

namespace lundukepaint {

StatusBar::StatusBar() : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8) {
  set_border_width(3);
  hint_.set_xalign(0.0f);
  hint_.set_hexpand(true);
  coords_.set_width_chars(14);
  sel_.set_width_chars(12);
  size_.set_width_chars(14);
  zoom_.set_width_chars(8);
  modified_.set_width_chars(10);
  pack_start(hint_, Gtk::PACK_EXPAND_WIDGET);
  pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  pack_start(coords_, Gtk::PACK_SHRINK);
  pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  pack_start(sel_, Gtk::PACK_SHRINK);
  pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  pack_start(size_, Gtk::PACK_SHRINK);
  pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  pack_start(zoom_, Gtk::PACK_SHRINK);
  pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  pack_start(modified_, Gtk::PACK_SHRINK);
  hint_.set_text("");
  set_canvas_size(800, 600);
  set_zoom(1.0);
  set_modified(false);
  set_selection_size(0, 0, false);
  clear_coordinates();
}

void StatusBar::set_hint(const Glib::ustring& hint) {
  hint_text_ = hint;
  if (!message_active_) {
    hint_.set_text(hint_text_);
  }
}

void StatusBar::show_coordinates(double x, double y) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%d, %d", static_cast<int>(x), static_cast<int>(y));
  coords_.set_text(buffer);
}

void StatusBar::clear_coordinates() {
  coords_.set_text("—, —");
}

void StatusBar::set_selection_size(int width, int height, bool visible) {
  if (!visible || width < 1 || height < 1) {
    sel_.set_text("");
    return;
  }
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "sel %d × %d", width, height);
  sel_.set_text(buffer);
}

void StatusBar::set_canvas_size(int width, int height) {
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%d × %d", width, height);
  size_.set_text(buffer);
}

void StatusBar::set_zoom(double zoom) {
  char buffer[32];
  std::snprintf(buffer, sizeof(buffer), "%d%%", static_cast<int>(zoom * 100.0 + 0.5));
  zoom_.set_text(buffer);
}

void StatusBar::set_modified(bool modified) {
  modified_.set_text(modified ? "Modified" : "");
}

void StatusBar::show_message(const Glib::ustring& message) {
  message_active_ = true;
  hint_.set_text(message);
  message_timer_.disconnect();
  message_timer_ = Glib::signal_timeout().connect(
      [this]() {
        message_active_ = false;
        hint_.set_text(hint_text_);
        return false;
      },
      4000);
}

}  // namespace lundukepaint
