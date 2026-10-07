// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/dialogs_adjust.hpp"

#include <glibmm/main.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>

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

}  // namespace lundukepaint
