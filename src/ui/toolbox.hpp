// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_TOOLBOX_HPP
#define LUNDUKEPAINT_UI_TOOLBOX_HPP

#include <functional>
#include <glibmm/refptr.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/cssprovider.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <string>
#include <vector>

#include "raster/types.hpp"
#include "ui/tool_selection.hpp"

namespace lundukepaint {

class Toolbox : public Gtk::Box {
public:
  Toolbox();

  void add_tool_button(const std::string& id, const std::string& tooltip, const std::string& icon_name);
  void set_active_tool(const std::string& id);
  void set_colors(Color fg, Color bg);

  const std::string& active_tool_id() const { return selection_.active_id(); }
  // True when the button for id currently carries the selected-tool highlight.
  bool tool_button_selected(const std::string& id) const;

  // Layout checks used by the widget tests (need a realized display).
  bool tool_columns_homogeneous() const;
  bool tool_columns_equal_width() const;
  // True when the toolbox width is essentially the tool grid (+ small border).
  bool width_tracks_tool_grid() const;
  bool fg_label_right_of_well() const;
  bool bg_label_left_of_well() const;
  bool bg_well_right_justified() const;
  bool bg_well_below_fg() const;

  std::function<void(const std::string& id)> on_tool_chosen;
  std::function<void(bool background)> on_well_clicked;
  std::function<void(bool background)> on_transparent;

private:
  static void ensure_css();
  void apply_selection_style();
  void draw_swatch(const Cairo::RefPtr<Cairo::Context>& cr, Gtk::DrawingArea& area, Color c);
  bool on_fg_well_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_bg_well_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_fg_well_press(GdkEventButton* event);
  bool on_bg_well_press(GdkEventButton* event);
  bool on_trans_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_trans_press(GdkEventButton* event);
  bool child_origin(const Gtk::Widget& child, int& x, int& y) const;
  void on_grid_size_allocate(Gtk::Allocation& allocation);
  void get_preferred_width_vfunc(int& minimum_width, int& natural_width) const override;
  int tool_grid_natural_width() const;

  Gtk::Grid grid_;
  Gtk::Box fg_row_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Box bg_row_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::DrawingArea fg_well_;
  Gtk::DrawingArea bg_well_;
  Gtk::Label fg_label_{"FG"};
  Gtk::Label bg_label_{"BG"};
  Gtk::DrawingArea trans_;
  std::vector<Gtk::Button*> buttons_;
  ToolSelection selection_;
  Color fg_ = Color::black();
  Color bg_ = Color::white();
  int next_col_ = 0;
  int next_row_ = 0;
};

}  // namespace lundukepaint

#endif
