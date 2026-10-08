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

// The left rail is about as wide as the two-column tool grid. Labels wrap
// and the font combo ellipsizes so the options sit in that column.

inline void configure_rail_check(Gtk::CheckButton& button) {
  button.set_halign(Gtk::ALIGN_START);
  if (auto* label = dynamic_cast<Gtk::Label*>(button.get_child())) {
    label->set_line_wrap(true);
    label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
    label->set_max_width_chars(10);
    label->set_width_chars(10);
    label->set_xalign(0.0f);
  }
}

inline void configure_rail_combo(Gtk::ComboBoxText& combo) {
  combo.set_hexpand(false);
  combo.set_halign(Gtk::ALIGN_FILL);
  combo.set_size_request(52, -1);
  for (auto* cell : combo.get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell)) {
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
      text->property_width_chars() = 6;
    }
  }
}

inline void configure_rail_spin(Gtk::SpinButton& spin) {
  spin.set_hexpand(false);
  spin.set_halign(Gtk::ALIGN_FILL);
  spin.set_width_chars(3);
  spin.set_size_request(52, -1);
}

inline void prepare_rail_box(Gtk::Box& box) {
  box.set_hexpand(false);
  box.set_halign(Gtk::ALIGN_FILL);
  box.get_style_context()->add_class("toolbox-options");
}

}  // namespace lundukepaint

#endif
