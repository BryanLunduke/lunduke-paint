// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/dialogs_new.hpp"

#include <cstdint>
#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

namespace lundukepaint {

NewImageDialog::NewImageDialog(Gtk::Window& parent, int width, int height)
    : Gtk::Dialog("New Image", parent, true),
      white_("White"),
      transparent_("Transparent"),
      custom_("Color") {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  width_.set_range(1, kHardMaxSide);
  width_.set_increments(1, 50);
  width_.set_value(width);
  width_.set_digits(0);
  width_.set_activates_default(true);
  height_.set_range(1, kHardMaxSide);
  height_.set_increments(1, 50);
  height_.set_value(height);
  height_.set_digits(0);
  height_.set_activates_default(true);

  Gtk::RadioButton::Group group = white_.get_group();
  transparent_.set_group(group);
  custom_.set_group(group);
  white_.set_active(true);

  Gdk::RGBA rgba;
  rgba.set_rgba(1.0, 1.0, 1.0, 1.0);
  color_.set_rgba(rgba);
  color_.set_use_alpha(true);
  color_.set_sensitive(false);
  custom_.signal_toggled().connect([this]() { color_.set_sensitive(custom_.get_active()); });

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Width")), 0, 0, 1, 1);
  grid->attach(width_, 1, 0, 2, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Height")), 0, 1, 1, 1);
  grid->attach(height_, 1, 1, 2, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Background")), 0, 2, 1, 1);
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  box->pack_start(white_, Gtk::PACK_SHRINK);
  box->pack_start(transparent_, Gtk::PACK_SHRINK);
  box->pack_start(custom_, Gtk::PACK_SHRINK);
  box->pack_start(color_, Gtk::PACK_SHRINK);
  grid->attach(*box, 1, 2, 2, 1);

  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int NewImageDialog::image_width() const {
  return width_.get_value_as_int();
}

int NewImageDialog::image_height() const {
  return height_.get_value_as_int();
}

Color NewImageDialog::background_color() const {
  if (transparent_.get_active()) {
    return Color::transparent();
  }
  if (custom_.get_active()) {
    const Gdk::RGBA rgba = color_.get_rgba();
    Color color;
    color.r = static_cast<std::uint8_t>(rgba.get_red() * 255.0 + 0.5);
    color.g = static_cast<std::uint8_t>(rgba.get_green() * 255.0 + 0.5);
    color.b = static_cast<std::uint8_t>(rgba.get_blue() * 255.0 + 0.5);
    color.a = static_cast<std::uint8_t>(rgba.get_alpha() * 255.0 + 0.5);
    return color;
  }
  return Color::white();
}

bool NewImageDialog::oversized() const {
  return image_width() > kSoftMaxSide || image_height() > kSoftMaxSide;
}

}  // namespace lundukepaint
