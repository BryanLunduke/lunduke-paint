// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_PATTERN_STRIP_HPP
#define LUNDUKEPAINT_UI_PATTERN_STRIP_HPP

#include "raster/pattern.hpp"
#include "raster/types.hpp"

#include <functional>
#include <gtkmm/box.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/fixed.h>
#include <gtkmm/scrolledwindow.h>
#include <vector>

namespace lundukepaint {

// MacPaint-style pattern strip: current pattern + FG/BG wells + swatch row.
class PatternStrip : public Gtk::Box {
public:
  PatternStrip();

  void set_colors(Color fg, Color bg);
  void set_pattern_index(int index);
  int pattern_index() const { return pattern_index_; }

  std::function<void(int index)> on_pattern_chosen;
  std::function<void(bool background)> on_well_clicked;
  std::function<void(bool background)> on_transparent;

private:
  bool on_current_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_fg_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_bg_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_fg_press(GdkEventButton* event);
  bool on_bg_press(GdkEventButton* event);
  bool on_swatch_draw(Gtk::DrawingArea* area, const Cairo::RefPtr<Cairo::Context>& cr, int index);
  bool on_swatch_press(GdkEventButton* event, int index);
  void draw_pattern(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, int index) const;
  void draw_well(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, Color c) const;

  Gtk::DrawingArea current_;
  Gtk::DrawingArea fg_well_;
  Gtk::DrawingArea bg_well_;
  Gtk::ScrolledWindow scroll_;
  Gtk::Box swatches_{Gtk::ORIENTATION_HORIZONTAL, 1};
  std::vector<Gtk::DrawingArea*> areas_;
  Color fg_ = Color::black();
  Color bg_ = Color::white();
  int pattern_index_ = 0;
};

}  // namespace lundukepaint

#endif
