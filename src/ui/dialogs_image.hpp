// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_DIALOGS_IMAGE_HPP
#define LUNDUKEPAINT_UI_DIALOGS_IMAGE_HPP

#include "raster/types.hpp"

#include <gtkmm/checkbutton.h>
#include <gtkmm/dialog.h>
#include <gtkmm/radiobutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>

namespace lundukepaint {

class CanvasSizeDialog : public Gtk::Dialog {
public:
  CanvasSizeDialog(Gtk::Window& parent, int width, int height);

  int image_width() const;
  int image_height() const;
  Color fill_color(Color background) const;
  bool oversized() const;

private:
  Gtk::SpinButton width_;
  Gtk::SpinButton height_;
  Gtk::RadioButton bg_;
  Gtk::RadioButton transparent_;
};

class ScaleImageDialog : public Gtk::Dialog {
public:
  ScaleImageDialog(Gtk::Window& parent, int width, int height);

  int image_width() const;
  int image_height() const;
  bool nearest() const;
  bool oversized() const;
  ~ScaleImageDialog() override;

private:
  void on_width_changed();
  void on_height_changed();

  int orig_w_ = 1;
  int orig_h_ = 1;
  bool updating_ = false;
  // Set in the destructor before the spins are destroyed. A dying spin still
  // emits value-changed, and the aspect check would then touch a dead widget.
  bool closing_ = false;
  Gtk::SpinButton width_;
  Gtk::SpinButton height_;
  Gtk::CheckButton keep_aspect_;
  Gtk::RadioButton nearest_;
  Gtk::RadioButton bilinear_;
};

}  // namespace lundukepaint

#endif
