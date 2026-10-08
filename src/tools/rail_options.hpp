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

// The left rail is wide enough for a spin button and a check label. Long
// font names ellipsize inside that width instead of being squeezed to a few
// characters.

inline void configure_rail_check(Gtk::CheckButton& button) {
  button.set_halign(Gtk::ALIGN_START);
  button.set_hexpand(true);
  if (auto* label = dynamic_cast<Gtk::Label*>(button.get_child())) {
    label->set_line_wrap(true);
    label->set_line_wrap_mode(Pango::WRAP_WORD_CHAR);
    label->set_max_width_chars(22);
    label->set_xalign(0.0f);
  }
}

inline void configure_rail_combo(Gtk::ComboBoxText& combo) {
  combo.set_hexpand(true);
  combo.set_halign(Gtk::ALIGN_FILL);
  for (auto* cell : combo.get_cells()) {
    if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cell)) {
      text->property_ellipsize() = Pango::ELLIPSIZE_END;
      text->property_width_chars() = 16;
    }
  }
}

inline void configure_rail_spin(Gtk::SpinButton& spin) {
  spin.set_hexpand(true);
  spin.set_halign(Gtk::ALIGN_FILL);
  spin.set_width_chars(4);
}

inline void prepare_rail_box(Gtk::Box& box) {
  box.set_hexpand(false);
  box.set_halign(Gtk::ALIGN_FILL);
  box.get_style_context()->add_class("toolbox-options");
}

}  // namespace lundukepaint

#endif
