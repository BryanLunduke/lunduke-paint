// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_COLORS_PANEL_HPP
#define LUNDUKEPAINT_UI_COLORS_PANEL_HPP

#include "raster/types.hpp"

#include <functional>
#include <gtkmm/box.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <vector>

namespace lundukepaint {

// Column-of-shades palette (light→dark per hue) + Custom row with "+".
// L=FG, M=BG, R=store FG into swatch (same as 0.5-5).
class ColorsPanel : public Gtk::Box {
public:
  ColorsPanel();

  void set_colors(Color fg, Color bg);

  std::function<void(Color color, bool background)> on_swatch;

private:
  enum class SlotKind { Palette, Custom, Add };

  void build_palette_grid();
  void rebuild_custom_row();
  Gtk::DrawingArea* make_swatch(SlotKind kind, int index, const char* tip);
  void queue_all_draws();
  bool on_swatch_draw(Gtk::DrawingArea* area, const Cairo::RefPtr<Cairo::Context>& cr,
                      SlotKind kind, int index);
  bool on_swatch_press(GdkEventButton* event, SlotKind kind, int index);
  void add_custom_color();
  Color slot_color(SlotKind kind, int index) const;
  bool color_matches_fg(Color c) const;

  Gtk::Grid palette_grid_;
  Gtk::Label custom_label_;
  Gtk::Box custom_row_;
  Gtk::Label hint_;

  Color fg_ = Color::black();
  Color bg_ = Color::white();
  std::vector<Color> palette_;
  std::vector<Color> custom_;
  std::vector<Gtk::DrawingArea*> palette_areas_;
  std::vector<Gtk::DrawingArea*> custom_areas_;
  Gtk::DrawingArea* add_area_ = nullptr;

  static constexpr int kCols = 9;
  static constexpr int kRows = 5;
  static constexpr int kSwatchW = 20;
  static constexpr int kSwatchH = 24;
  static constexpr int kMaxCustom = 12;
};

}  // namespace lundukepaint

#endif
