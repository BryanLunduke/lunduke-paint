// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_PATTERN_STRIP_HPP
#define LUNDUKEPAINT_UI_PATTERN_STRIP_HPP

#include "raster/pattern.hpp"
#include "raster/types.hpp"

#include <functional>
#include <gtkmm/box.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/grid.h>
#include <gtkmm/scrolledwindow.h>
#include <vector>

namespace lundukepaint {

// MacPaint-style pattern strip: two rows of pattern swatches (no FG/BG wells;
// color picking lives in the Colors panel).
class PatternStrip : public Gtk::Box {
public:
  PatternStrip();

  void set_colors(Color fg, Color bg);
  void set_pattern_index(int index);
  int pattern_index() const { return pattern_index_; }

  std::function<void(int index)> on_pattern_chosen;

private:
  bool on_swatch_draw(Gtk::DrawingArea* area, const Cairo::RefPtr<Cairo::Context>& cr, int index);
  bool on_swatch_press(GdkEventButton* event, int index);
  void draw_pattern(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, int index) const;

  Gtk::ScrolledWindow scroll_;
  Gtk::Grid swatches_;
  std::vector<Gtk::DrawingArea*> areas_;
  Color fg_ = Color::black();
  Color bg_ = Color::white();
  int pattern_index_ = 0;
};

}  // namespace lundukepaint

#endif
