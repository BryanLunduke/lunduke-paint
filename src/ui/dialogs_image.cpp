// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/dialogs_image.hpp"

#include <algorithm>
#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

namespace lundukepaint {

CanvasSizeDialog::CanvasSizeDialog(Gtk::Window& parent, int width, int height)
    : Gtk::Dialog("Canvas Size", parent, true), bg_("Background color"),
      transparent_("Transparent") {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  width_.set_range(1, kHardMaxSide);
  width_.set_increments(1, 50);
  width_.set_digits(0);
  width_.set_value(width);
  width_.set_activates_default(true);
  height_.set_range(1, kHardMaxSide);
  height_.set_increments(1, 50);
  height_.set_digits(0);
  height_.set_value(height);
  height_.set_activates_default(true);

  Gtk::RadioButton::Group group = bg_.get_group();
  transparent_.set_group(group);
  bg_.set_active(true);

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Width")), 0, 0, 1, 1);
  grid->attach(width_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Height")), 0, 1, 1, 1);
  grid->attach(height_, 1, 1, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("New pixels")), 0, 2, 1, 1);
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  box->pack_start(bg_, Gtk::PACK_SHRINK);
  box->pack_start(transparent_, Gtk::PACK_SHRINK);
  grid->attach(*box, 1, 2, 1, 1);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int CanvasSizeDialog::image_width() const {
  return width_.get_value_as_int();
}

int CanvasSizeDialog::image_height() const {
  return height_.get_value_as_int();
}

Color CanvasSizeDialog::fill_color(Color background) const {
  return transparent_.get_active() ? Color::transparent() : background;
}

bool CanvasSizeDialog::oversized() const {
  return image_width() > kSoftMaxSide || image_height() > kSoftMaxSide;
}

ScaleImageDialog::ScaleImageDialog(Gtk::Window& parent, int width, int height)
    : Gtk::Dialog("Scale Image", parent, true), orig_w_(width), orig_h_(height),
      keep_aspect_("Keep aspect ratio"), nearest_("Nearest neighbor"),
      bilinear_("Bilinear") {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  width_.set_range(1, kHardMaxSide);
  width_.set_increments(1, 50);
  width_.set_digits(0);
  width_.set_value(width);
  width_.set_activates_default(true);
  height_.set_range(1, kHardMaxSide);
  height_.set_increments(1, 50);
  height_.set_digits(0);
  height_.set_value(height);
  height_.set_activates_default(true);
  keep_aspect_.set_active(true);

  Gtk::RadioButton::Group group = nearest_.get_group();
  bilinear_.set_group(group);
  nearest_.set_active(true);

  width_.signal_value_changed().connect(sigc::mem_fun(*this, &ScaleImageDialog::on_width_changed));
  height_.signal_value_changed().connect(sigc::mem_fun(*this, &ScaleImageDialog::on_height_changed));

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Width")), 0, 0, 1, 1);
  grid->attach(width_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Height")), 0, 1, 1, 1);
  grid->attach(height_, 1, 1, 1, 1);
  grid->attach(keep_aspect_, 1, 2, 1, 1);
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 8));
  box->pack_start(nearest_, Gtk::PACK_SHRINK);
  box->pack_start(bilinear_, Gtk::PACK_SHRINK);
  grid->attach(*Gtk::manage(new Gtk::Label("Interpolation")), 0, 3, 1, 1);
  grid->attach(*box, 1, 3, 1, 1);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

ScaleImageDialog::~ScaleImageDialog() {
  closing_ = true;
}

void ScaleImageDialog::on_width_changed() {
  if (closing_ || updating_ || !keep_aspect_.get_active() || orig_w_ < 1) {
    return;
  }
  updating_ = true;
  const int w = width_.get_value_as_int();
  const int h = std::max(1, static_cast<int>(static_cast<long>(w) * orig_h_ / orig_w_));
  height_.set_value(h);
  updating_ = false;
}

void ScaleImageDialog::on_height_changed() {
  if (closing_ || updating_ || !keep_aspect_.get_active() || orig_h_ < 1) {
    return;
  }
  updating_ = true;
  const int h = height_.get_value_as_int();
  const int w = std::max(1, static_cast<int>(static_cast<long>(h) * orig_w_ / orig_h_));
  width_.set_value(w);
  updating_ = false;
}

int ScaleImageDialog::image_width() const {
  return width_.get_value_as_int();
}

int ScaleImageDialog::image_height() const {
  return height_.get_value_as_int();
}

bool ScaleImageDialog::nearest() const {
  return nearest_.get_active();
}

bool ScaleImageDialog::oversized() const {
  return image_width() > kSoftMaxSide || image_height() > kSoftMaxSide;
}

}  // namespace lundukepaint
