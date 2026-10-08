// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_TOOLS_RAIL_OPTIONS_HPP
#define LUNDUKEPAINT_TOOLS_RAIL_OPTIONS_HPP

#include <gtkmm/box.h>
#include <gtkmm/cellrenderertext.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

namespace lundukepaint {

// The left rail is the classic two-column tool grid (82 px outside, about
// 58 px for the control itself). Check labels and the font combo ellipsize
// so the options sit in that column instead of widening it.

inline void configure_rail_check(Gtk::CheckButton& button) {
  button.set_halign(Gtk::ALIGN_FILL);
  button.set_hexpand(true);
  if (auto* label = dynamic_cast<Gtk::Label*>(button.get_child())) {
    // One line, ellipsized to the column. Wrapping makes GTK report a
    // minimum height for the narrowest width, taller than the rail allocates.
    label->set_ellipsize(Pango::ELLIPSIZE_END);
    label->set_width_chars(4);
    label->set_max_width_chars(4);
    label->set_xalign(0.0f);
  }
}

inline void configure_rail_combo(Gtk::ComboBoxText& combo) {
  combo.set_hexpand(true);
  combo.set_halign(Gtk::ALIGN_FILL);
  combo.set_size_request(48, -1);
  for (auto* cell : combo.get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell)) {
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
      text->property_width_chars() = 4;
      text->property_max_width_chars() = 4;
    }
  }
}

inline void configure_rail_spin(Gtk::SpinButton& spin) {
  spin.set_hexpand(true);
  spin.set_halign(Gtk::ALIGN_FILL);
  // width-chars 3 measures about 63 px, which is wider than the column.
  spin.set_width_chars(2);
}

inline void prepare_rail_box(Gtk::Box& box) {
  box.set_hexpand(false);
  box.set_halign(Gtk::ALIGN_FILL);
  box.get_style_context()->add_class("toolbox-options");
}

}  // namespace lundukepaint

#endif
