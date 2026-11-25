// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_DIALOGS_PREFS_HPP
#define LUNDUKEPAINT_UI_DIALOGS_PREFS_HPP

#include "app/preferences.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/colorbutton.h>
#include <gtkmm/dialog.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>

namespace lundukepaint {

class PreferencesDialog : public Gtk::Dialog {
public:
  PreferencesDialog(Gtk::Window& parent, const Preferences& prefs);

  void apply_to(Preferences& prefs) const;

private:
  Gtk::SpinButton width_;
  Gtk::SpinButton height_;
  Gtk::SpinButton undo_limit_;
  Gtk::ColorButton checker_light_;
  Gtk::ColorButton checker_dark_;
  Gtk::SpinButton grid_threshold_;
  Gtk::CheckButton howdy_;
};

}  // namespace lundukepaint

#endif
