// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_TOOLS_TOOL_OPTIONS_UI_HPP
#define LUNDUKEPAINT_TOOLS_TOOL_OPTIONS_UI_HPP

// Compact per-tool option widgets for the narrow left toolbox column (~92px).

#include <gtkmm/box.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/label.h>
#include <gtkmm/spinbutton.h>

#include <memory>

namespace lundukepaint {
namespace tool_options_ui {

inline std::unique_ptr<Gtk::Box> make_column() {
  auto box = std::make_unique<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 2);
  box->set_hexpand(false);
  box->set_halign(Gtk::ALIGN_FILL);
  box->set_valign(Gtk::ALIGN_START);
  return box;
}

inline Gtk::SpinButton* make_spin(double lo, double hi, double step, double value,
                                   double page = 0.0) {
  auto* spin = Gtk::manage(new Gtk::SpinButton());
  spin->set_range(lo, hi);
  spin->set_increments(step, page > 0.0 ? page : step * 4.0);
  spin->set_digits(0);
  spin->set_value(value);
  spin->set_width_chars(3);
  spin->set_alignment(1.0f);
  spin->set_hexpand(true);
  spin->set_halign(Gtk::ALIGN_FILL);
  return spin;
}

// Label above a control (fits the narrow column better than side-by-side).
inline void pack_labeled(Gtk::Box& column, const char* text, const char* tip, Gtk::Widget& control) {
  auto* label = Gtk::manage(new Gtk::Label(text));
  label->set_halign(Gtk::ALIGN_START);
  label->set_xalign(0.0f);
  if (tip != nullptr && tip[0] != '\0') {
    label->set_tooltip_text(tip);
    control.set_tooltip_text(tip);
  }
  column.pack_start(*label, Gtk::PACK_SHRINK);
  column.pack_start(control, Gtk::PACK_SHRINK);
}

inline Gtk::CheckButton* make_check(const char* text, const char* tip, bool active) {
  auto* btn = Gtk::manage(new Gtk::CheckButton(text));
  btn->set_active(active);
  btn->set_halign(Gtk::ALIGN_START);
  if (tip != nullptr && tip[0] != '\0') {
    btn->set_tooltip_text(tip);
  }
  return btn;
}

inline void constrain_combo(Gtk::ComboBoxText& combo) {
  combo.set_hexpand(true);
  combo.set_halign(Gtk::ALIGN_FILL);
  // Soft cap so the toolbox preferred-width clamp is not fighting a huge combo.
  combo.set_size_request(64, -1);
}

}  // namespace tool_options_ui
}  // namespace lundukepaint

#endif
