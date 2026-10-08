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
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/stack.h>
#include <string>
#include <vector>

#include "raster/types.hpp"
#include "ui/tool_selection.hpp"

namespace lundukepaint {

// Left MacPaint toolbox: tool grid + compact left-rail options (line width /
// brush tips / spray radius) that swap with the active tool — not a strip
// under the canvas.
class Toolbox : public Gtk::Box {
public:
  Toolbox();

  void add_tool_button(const std::string& id, const std::string& tooltip, const std::string& icon_name);
  void set_active_tool(const std::string& id);
  void set_line_width(int width);
  int line_width() const { return line_width_; }
  void set_brush_tip(int index);
  int brush_tip() const { return brush_tip_; }
  void set_spray_radius(int radius);
  int spray_radius() const { return spray_radius_; }
  // Show the matching left-rail options panel for this tool id.
  void show_options_for_tool(const std::string& id);
  // Parent the active tool's own controls under the width/brush/spray picker.
  // Passing null hides that block. The toolbox does not own the widget.
  void set_tool_options(Gtk::Widget* widget);

  void set_colors(Color fg, Color bg);
  int tool_button_width() const;
  int line_width_picker_width() const;

  const std::string& active_tool_id() const { return selection_.active_id(); }
  bool tool_button_selected(const std::string& id) const;

  bool tool_columns_homogeneous() const;
  bool tool_columns_equal_width() const;
  bool width_tracks_tool_grid() const;

  std::function<void(const std::string& id)> on_tool_chosen;
  std::function<void(int width)> on_line_width_chosen;
  std::function<void(int index)> on_brush_tip_chosen;
  std::function<void(int radius)> on_spray_radius_chosen;
  std::function<void()> on_swap_colors;
  std::function<void()> on_reset_colors;
  std::function<void(bool background)> on_edit_color;

private:
  static void ensure_css();
  void apply_selection_style();
  void on_grid_size_allocate(Gtk::Allocation& allocation);
  void get_preferred_width_vfunc(int& minimum_width, int& natural_width) const override;
  int tool_grid_natural_width() const;
  bool on_line_width_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_line_width_press(GdkEventButton* event);
  bool on_brush_tips_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_brush_tips_press(GdkEventButton* event);
  bool on_spray_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_spray_press(GdkEventButton* event);
  bool on_wells_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_wells_press(GdkEventButton* event);
  bool on_swap_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_swap_press(GdkEventButton* event);
  bool on_reset_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_reset_press(GdkEventButton* event);
  int classic_outer_width() const;
  int width_at_y(double y) const;
  int brush_tip_at(double x, double y) const;
  int spray_radius_at_y(double y) const;
  void size_option_panels();

  Gtk::ScrolledWindow scroll_;
  Gtk::Box rail_{Gtk::ORIENTATION_VERTICAL, 2};
  Gtk::Grid grid_;
  Gtk::Stack options_stack_;
  Gtk::DrawingArea line_widths_;
  Gtk::DrawingArea brush_tips_;
  Gtk::DrawingArea spray_radii_;
  Gtk::Box empty_options_{Gtk::ORIENTATION_VERTICAL};
  Gtk::Box tool_options_{Gtk::ORIENTATION_VERTICAL};
  Gtk::Box color_row_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::DrawingArea wells_;
  Gtk::DrawingArea swap_colors_;
  Gtk::DrawingArea reset_colors_;
  Gtk::Widget* hosted_options_{nullptr};
  Color fg_ = Color::black();
  Color bg_ = Color::white();
  std::vector<Gtk::Button*> buttons_;
  ToolSelection selection_;
  int next_col_ = 0;
  int next_row_ = 0;
  int line_width_ = 1;
  int brush_tip_ = 3;  // Round 8 by default
  int spray_radius_ = 16;
};

}  // namespace lundukepaint

#endif
