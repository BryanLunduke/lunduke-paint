// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/dialogs_prefs.hpp"

#include <gtkmm/grid.h>
#include <gtkmm/label.h>

namespace lundukepaint {
namespace {

Gdk::RGBA color_to_rgba(Color c) {
  Gdk::RGBA rgba;
  rgba.set_rgba(c.r / 255.0, c.g / 255.0, c.b / 255.0, 1.0);
  return rgba;
}

Color rgba_to_color(const Gdk::RGBA& rgba) {
  Color c;
  c.r = static_cast<std::uint8_t>(rgba.get_red() * 255.0 + 0.5);
  c.g = static_cast<std::uint8_t>(rgba.get_green() * 255.0 + 0.5);
  c.b = static_cast<std::uint8_t>(rgba.get_blue() * 255.0 + 0.5);
  c.a = 255;
  return c;
}

}  // namespace

PreferencesDialog::PreferencesDialog(Gtk::Window& parent, const Preferences& prefs)
    : Gtk::Dialog("Preferences", parent, true) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  width_.set_range(1, kHardMaxSide);
  width_.set_increments(1, 50);
  width_.set_digits(0);
  width_.set_value(prefs.default_width);
  height_.set_range(1, kHardMaxSide);
  height_.set_increments(1, 50);
  height_.set_digits(0);
  height_.set_value(prefs.default_height);

  undo_limit_.set_range(1, kMaxUndoDepth);
  undo_limit_.set_increments(1, 10);
  undo_limit_.set_digits(0);
  undo_limit_.set_value(prefs.undo_limit);
  undo_limit_.set_tooltip_text(
      "How many steps to keep (1–200). Oldest steps are also dropped when undo memory exceeds the memory budget (at least 256 MB).");
  width_.set_activates_default(true);
  height_.set_activates_default(true);
  undo_limit_.set_activates_default(true);
  grid_threshold_.set_activates_default(true);

  checker_light_.set_rgba(color_to_rgba(prefs.checker_light));
  checker_light_.set_use_alpha(false);
  checker_dark_.set_rgba(color_to_rgba(prefs.checker_dark));
  checker_dark_.set_use_alpha(false);

  grid_threshold_.set_range(100, 1600);
  grid_threshold_.set_increments(100, 100);
  grid_threshold_.set_digits(0);
  grid_threshold_.set_value(prefs.grid_threshold);

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Default width")), 0, 0, 1, 1);
  grid->attach(width_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Default height")), 0, 1, 1, 1);
  grid->attach(height_, 1, 1, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Undo limit")), 0, 2, 1, 1);
  grid->attach(undo_limit_, 1, 2, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Checker light")), 0, 3, 1, 1);
  grid->attach(checker_light_, 1, 3, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Checker dark")), 0, 4, 1, 1);
  grid->attach(checker_dark_, 1, 4, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Grid at zoom %")), 0, 5, 1, 1);
  grid->attach(grid_threshold_, 1, 5, 1, 1);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

void PreferencesDialog::apply_to(Preferences& prefs) const {
  prefs.default_width = width_.get_value_as_int();
  prefs.default_height = height_.get_value_as_int();
  prefs.undo_limit = undo_limit_.get_value_as_int();
  prefs.checker_light = rgba_to_color(checker_light_.get_rgba());
  prefs.checker_dark = rgba_to_color(checker_dark_.get_rgba());
  prefs.grid_threshold = grid_threshold_.get_value_as_int();
}

}  // namespace lundukepaint
