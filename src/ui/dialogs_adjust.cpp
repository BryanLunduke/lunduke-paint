// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/dialogs_adjust.hpp"

#include <glibmm/main.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

#include <algorithm>

namespace lundukepaint {
namespace {

void setup_scale(Gtk::Scale& scale, double lo, double hi, double value) {
  scale.set_range(lo, hi);
  scale.set_increments(1, 10);
  scale.set_digits(0);
  scale.set_value(value);
  scale.set_draw_value(true);
  scale.set_value_pos(Gtk::POS_RIGHT);
  scale.set_hexpand(true);
  scale.set_size_request(220, -1);
}

}  // namespace

LivePreviewDialog::LivePreviewDialog(const Glib::ustring& title, Gtk::Window& parent)
    : Gtk::Dialog(title, parent, true) {
  live_.set_tooltip_text("Apply the change to the canvas while you drag");
  live_.signal_toggled().connect(sigc::mem_fun(*this, &LivePreviewDialog::on_live_toggled));
}

LivePreviewDialog::~LivePreviewDialog() {
  preview_delay_.disconnect();
}

void LivePreviewDialog::add_live_preview(Gtk::Grid& grid, int row, int width) {
  grid.attach(live_, 0, row, width, 1);
}

void LivePreviewDialog::watch(Gtk::Range& range) {
  range.signal_value_changed().connect(sigc::mem_fun(*this, &LivePreviewDialog::fire_preview));
}

void LivePreviewDialog::watch(Gtk::SpinButton& spin) {
  spin.signal_value_changed().connect(sigc::mem_fun(*this, &LivePreviewDialog::fire_preview));
}

void LivePreviewDialog::on_live_toggled() {
  if (live_.get_active()) {
    fire_preview();
  } else if (on_preview_reset) {
    on_preview_reset();
  }
}

void LivePreviewDialog::note_changed() {
  fire_preview();
}

void LivePreviewDialog::fire_preview() {
  preview_delay_.disconnect();
  if (!live_.get_active() || !on_preview) {
    return;
  }
  // The last slider value wins. A burst of ticks does not rerun the effect.
  preview_delay_ = Glib::signal_timeout().connect(
      [this]() {
        if (live_.get_active() && on_preview) {
          on_preview();
        }
        return false;
      },
      40);
}

BrightnessContrastDialog::BrightnessContrastDialog(Gtk::Window& parent)
    : LivePreviewDialog("Brightness / Contrast", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  setup_scale(brightness_, -100, 100, 0);
  setup_scale(contrast_, -100, 100, 0);

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Brightness")), 0, 0, 1, 1);
  grid->attach(brightness_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Contrast")), 0, 1, 1, 1);
  grid->attach(contrast_, 1, 1, 1, 1);
  add_live_preview(*grid, 2);
  watch(brightness_);
  watch(contrast_);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int BrightnessContrastDialog::brightness() const {
  return static_cast<int>(brightness_.get_value());
}

int BrightnessContrastDialog::contrast() const {
  return static_cast<int>(contrast_.get_value());
}

HueSaturationDialog::HueSaturationDialog(Gtk::Window& parent)
    : LivePreviewDialog("Hue / Saturation", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  setup_scale(hue_, -180, 180, 0);
  setup_scale(saturation_, -100, 100, 0);

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Hue")), 0, 0, 1, 1);
  grid->attach(hue_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Saturation")), 0, 1, 1, 1);
  grid->attach(saturation_, 1, 1, 1, 1);
  add_live_preview(*grid, 2);
  watch(hue_);
  watch(saturation_);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int HueSaturationDialog::hue() const {
  return static_cast<int>(hue_.get_value());
}

int HueSaturationDialog::saturation() const {
  return static_cast<int>(saturation_.get_value());
}

PosterizeDialog::PosterizeDialog(Gtk::Window& parent) : LivePreviewDialog("Posterize", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  levels_.set_range(2, 16);
  levels_.set_increments(1, 2);
  levels_.set_digits(0);
  levels_.set_value(4);
  levels_.set_activates_default(true);

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Levels")), 0, 0, 1, 1);
  grid->attach(levels_, 1, 0, 1, 1);
  add_live_preview(*grid, 1);
  watch(levels_);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int PosterizeDialog::levels() const {
  return levels_.get_value_as_int();
}

BlurDialog::BlurDialog(Gtk::Window& parent) : LivePreviewDialog("Blur", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);

  radius_.set_range(1, 16);
  radius_.set_increments(1, 2);
  radius_.set_digits(0);
  radius_.set_value(2);
  radius_.set_activates_default(true);

  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  grid->attach(*Gtk::manage(new Gtk::Label("Radius")), 0, 0, 1, 1);
  grid->attach(radius_, 1, 0, 1, 1);
  add_live_preview(*grid, 1);
  watch(radius_);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int BlurDialog::radius() const {
  return radius_.get_value_as_int();
}

namespace {

Gtk::Grid* labeled_grid() {
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(8);
  grid->set_column_spacing(8);
  grid->set_border_width(8);
  return grid;
}

void setup_spin(Gtk::SpinButton& spin, double lo, double hi, double value) {
  spin.set_range(lo, hi);
  spin.set_increments(1, 4);
  spin.set_digits(0);
  spin.set_value(value);
  spin.set_activates_default(true);
}

}  // namespace

HalftoneDialog::HalftoneDialog(Gtk::Window& parent) : LivePreviewDialog("Halftone / Newsprint", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);
  screen_.append("Bayer");
  screen_.append("Clustered dot");
  screen_.set_active(0);
  setup_spin(cell_, 1, 16, 2);
  auto* grid = labeled_grid();
  grid->attach(*Gtk::manage(new Gtk::Label("Screen")), 0, 0, 1, 1);
  grid->attach(screen_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Cell")), 0, 1, 1, 1);
  grid->attach(cell_, 1, 1, 1, 1);
  add_live_preview(*grid, 2);
  watch(cell_);
  screen_.signal_changed().connect(sigc::mem_fun(*this, &HalftoneDialog::note_changed));
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int HalftoneDialog::screen() const {
  return std::max(0, screen_.get_active_row_number());
}

int HalftoneDialog::cell() const {
  return cell_.get_value_as_int();
}

DiffuseDialog::DiffuseDialog(Gtk::Window& parent) : LivePreviewDialog("Diffuse", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);
  setup_spin(radius_, 1, 32, 4);
  auto* grid = labeled_grid();
  grid->attach(*Gtk::manage(new Gtk::Label("Radius")), 0, 0, 1, 1);
  grid->attach(radius_, 1, 0, 1, 1);
  add_live_preview(*grid, 1);
  watch(radius_);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int DiffuseDialog::radius() const {
  return radius_.get_value_as_int();
}

DropShadowDialog::DropShadowDialog(Gtk::Window& parent) : LivePreviewDialog("Drop Shadow", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);
  setup_spin(dx_, -64, 64, 4);
  setup_spin(dy_, -64, 64, 4);
  auto* grid = labeled_grid();
  grid->attach(*Gtk::manage(new Gtk::Label("Offset X")), 0, 0, 1, 1);
  grid->attach(dx_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Offset Y")), 0, 1, 1, 1);
  grid->attach(dy_, 1, 1, 1, 1);
  auto* note = Gtk::manage(new Gtk::Label("Hard silhouette, painted in the background color"));
  note->set_halign(Gtk::ALIGN_START);
  grid->attach(*note, 0, 2, 2, 1);
  add_live_preview(*grid, 3);
  watch(dx_);
  watch(dy_);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int DropShadowDialog::offset_x() const {
  return dx_.get_value_as_int();
}

int DropShadowDialog::offset_y() const {
  return dy_.get_value_as_int();
}

WindDialog::WindDialog(Gtk::Window& parent) : LivePreviewDialog("Wind", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);
  direction_.append("Right");
  direction_.append("Left");
  direction_.append("Down");
  direction_.append("Up");
  direction_.set_active(0);
  setup_spin(strength_, 1, 48, 12);
  auto* grid = labeled_grid();
  grid->attach(*Gtk::manage(new Gtk::Label("Direction")), 0, 0, 1, 1);
  grid->attach(direction_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Strength")), 0, 1, 1, 1);
  grid->attach(strength_, 1, 1, 1, 1);
  add_live_preview(*grid, 2);
  watch(strength_);
  direction_.signal_changed().connect(sigc::mem_fun(*this, &WindDialog::note_changed));
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int WindDialog::direction() const {
  return std::max(0, direction_.get_active_row_number());
}

int WindDialog::strength() const {
  return strength_.get_value_as_int();
}

ScanlineDialog::ScanlineDialog(Gtk::Window& parent) : LivePreviewDialog("CRT Scanlines", parent) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);
  setup_spin(percent_, 10, 90, 45);
  auto* grid = labeled_grid();
  grid->attach(*Gtk::manage(new Gtk::Label("Darken odd rows")), 0, 0, 1, 1);
  grid->attach(percent_, 1, 0, 1, 1);
  add_live_preview(*grid, 1);
  watch(percent_);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int ScanlineDialog::percent() const {
  return percent_.get_value_as_int();
}

PixelCountDialog::PixelCountDialog(Gtk::Window& parent, const Glib::ustring& title, int initial)
    : Gtk::Dialog(title, parent, true) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);
  setup_spin(pixels_, 1, 64, initial);
  auto* grid = labeled_grid();
  grid->attach(*Gtk::manage(new Gtk::Label("Pixels")), 0, 0, 1, 1);
  grid->attach(pixels_, 1, 0, 1, 1);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

int PixelCountDialog::pixels() const {
  return pixels_.get_value_as_int();
}

RotateShearDialog::RotateShearDialog(Gtk::Window& parent) : Gtk::Dialog("Free Rotate and Shear", parent, true) {
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);
  set_resizable(false);
  degrees_.set_range(-180, 180);
  degrees_.set_increments(1, 15);
  degrees_.set_digits(0);
  degrees_.set_value(15);
  degrees_.set_activates_default(true);
  setup_spin(shear_x_, -128, 128, 0);
  setup_spin(shear_y_, -128, 128, 0);
  auto* grid = labeled_grid();
  grid->attach(*Gtk::manage(new Gtk::Label("Rotate °")), 0, 0, 1, 1);
  grid->attach(degrees_, 1, 0, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Shear X")), 0, 1, 1, 1);
  grid->attach(shear_x_, 1, 1, 1, 1);
  grid->attach(*Gtk::manage(new Gtk::Label("Shear Y")), 0, 2, 1, 1);
  grid->attach(shear_y_, 1, 2, 1, 1);
  auto* note = Gtk::manage(new Gtk::Label("Nearest neighbor. Clockwise degrees."));
  note->set_halign(Gtk::ALIGN_START);
  grid->attach(*note, 0, 3, 2, 1);
  get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  show_all();
}

double RotateShearDialog::degrees() const {
  return degrees_.get_value();
}

int RotateShearDialog::shear_x() const {
  return shear_x_.get_value_as_int();
}

int RotateShearDialog::shear_y() const {
  return shear_y_.get_value_as_int();
}

}  // namespace lundukepaint
