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
#include <string>
#include <vector>

#include "ui/tool_selection.hpp"

namespace lundukepaint {

class Toolbox : public Gtk::Box {
public:
  Toolbox();

  void add_tool_button(const std::string& id, const std::string& tooltip, const std::string& icon_name);
  void set_active_tool(const std::string& id);
  void set_line_width(int width);
  int line_width() const { return line_width_; }

  const std::string& active_tool_id() const { return selection_.active_id(); }
  bool tool_button_selected(const std::string& id) const;

  bool tool_columns_homogeneous() const;
  bool tool_columns_equal_width() const;
  bool width_tracks_tool_grid() const;

  std::function<void(const std::string& id)> on_tool_chosen;
  std::function<void(int width)> on_line_width_chosen;

private:
  static void ensure_css();
  void apply_selection_style();
  void on_grid_size_allocate(Gtk::Allocation& allocation);
  void get_preferred_width_vfunc(int& minimum_width, int& natural_width) const override;
  int tool_grid_natural_width() const;
  bool on_line_width_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_line_width_press(GdkEventButton* event);
  int width_at_y(double y) const;

  Gtk::Grid grid_;
  Gtk::DrawingArea line_widths_;
  std::vector<Gtk::Button*> buttons_;
  ToolSelection selection_;
  int next_col_ = 0;
  int next_row_ = 0;
  int line_width_ = 1;
};

}  // namespace lundukepaint

#endif
