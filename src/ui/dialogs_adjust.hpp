// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_DIALOGS_ADJUST_HPP
#define LUNDUKEPAINT_UI_DIALOGS_ADJUST_HPP

#include <functional>
#include <glibmm/ustring.h>
#include <sigc++/connection.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/dialog.h>
#include <gtkmm/grid.h>
#include <gtkmm/range.h>
#include <gtkmm/scale.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/window.h>

namespace lundukepaint {

// Shared "Live Preview" plumbing for the adjustment dialogs. With the check
// button on, every slider tick fires on_preview so the caller can repaint the
// active layer in place; turning it back off fires on_preview_reset so the
// caller can put the original pixels back. Nothing here touches the document.
class LivePreviewDialog : public Gtk::Dialog {
public:
  LivePreviewDialog(const Glib::ustring& title, Gtk::Window& parent);
  ~LivePreviewDialog() override;

  bool live_preview() const { return live_.get_active(); }

  // Fired on every value change while Live Preview is on.
  std::function<void()> on_preview;
  // Fired when Live Preview is switched off (restore the original pixels).
  std::function<void()> on_preview_reset;

protected:
  // Adds the check button as the last row of the dialog's grid.
  void add_live_preview(Gtk::Grid& grid, int row, int width = 2);
  void watch(Gtk::Range& range);
  void watch(Gtk::SpinButton& spin);

private:
  void on_live_toggled();
  void fire_preview();

  Gtk::CheckButton live_{"Live Preview"};
  sigc::connection preview_delay_;
};

class BrightnessContrastDialog : public LivePreviewDialog {
public:
  explicit BrightnessContrastDialog(Gtk::Window& parent);

  int brightness() const;
  int contrast() const;

private:
  Gtk::Scale brightness_;
  Gtk::Scale contrast_;
};

class HueSaturationDialog : public LivePreviewDialog {
public:
  explicit HueSaturationDialog(Gtk::Window& parent);

  int hue() const;
  int saturation() const;

private:
  Gtk::Scale hue_;
  Gtk::Scale saturation_;
};

class PosterizeDialog : public LivePreviewDialog {
public:
  explicit PosterizeDialog(Gtk::Window& parent);

  int levels() const;

private:
  Gtk::SpinButton levels_;
};

class BlurDialog : public LivePreviewDialog {
public:
  explicit BlurDialog(Gtk::Window& parent);

  int radius() const;

private:
  Gtk::SpinButton radius_;
};

}  // namespace lundukepaint

#endif
