// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/colors_panel.hpp"

#include <gtkmm/colorchooserdialog.h>
#include <gtkmm/widget.h>
#include <gtkmm/window.h>

#include <cmath>

namespace lundukepaint {
namespace {

Color rgb(int r, int g, int b) {
  return {static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
          static_cast<std::uint8_t>(b), 255};
}

// 9 hue columns × 5 shades (light → dark). Matches the reference vibe.
const Color kShadePalette[45] = {
    // Blue
    rgb(180, 220, 255), rgb(100, 170, 245), rgb(40, 110, 220), rgb(20, 60, 180), rgb(10, 30, 110),
    // Green
    rgb(190, 240, 190), rgb(110, 200, 110), rgb(40, 160, 60), rgb(20, 110, 40), rgb(10, 60, 25),
    // Yellow
    rgb(255, 250, 180), rgb(255, 235, 90), rgb(240, 200, 30), rgb(200, 150, 20), rgb(140, 100, 10),
    // Orange
    rgb(255, 220, 180), rgb(255, 170, 90), rgb(240, 120, 30), rgb(200, 80, 15), rgb(130, 45, 10),
    // Red / crimson
    rgb(255, 190, 195), rgb(255, 110, 120), rgb(220, 40, 50), rgb(160, 20, 35), rgb(100, 10, 25),
    // Purple / lavender
    rgb(230, 210, 255), rgb(180, 140, 230), rgb(130, 70, 200), rgb(90, 40, 150), rgb(50, 20, 90),
    // Brown / tan
    rgb(240, 220, 190), rgb(210, 170, 120), rgb(160, 110, 60), rgb(110, 70, 35), rgb(60, 35, 18),
    // Light greys (white → mid)
    rgb(255, 255, 255), rgb(235, 235, 235), rgb(210, 210, 210), rgb(185, 185, 185), rgb(160, 160, 160),
    // Dark greys → black
    rgb(130, 130, 130), rgb(95, 95, 95), rgb(60, 60, 60), rgb(30, 30, 30), rgb(0, 0, 0),
};

void path_rounded_rect(const Cairo::RefPtr<Cairo::Context>& cr, double x, double y, double w,
                       double h, double radius) {
  const double r = std::min(radius, std::min(w, h) / 2.0);
  cr->begin_new_sub_path();
  cr->arc(x + w - r, y + r, r, -M_PI / 2.0, 0.0);
  cr->arc(x + w - r, y + h - r, r, 0.0, M_PI / 2.0);
  cr->arc(x + r, y + h - r, r, M_PI / 2.0, M_PI);
  cr->arc(x + r, y + r, r, M_PI, 3.0 * M_PI / 2.0);
  cr->close_path();
}

double luminance(Color c) {
  return (0.299 * c.r + 0.587 * c.g + 0.114 * c.b) / 255.0;
}

void draw_checker(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
  const int cell = std::max(4, std::min(w, h) / 3);
  for (int y = 0; y < h; y += cell) {
    for (int x = 0; x < w; x += cell) {
      const bool dark = ((x / cell) + (y / cell)) % 2 == 0;
      if (dark) {
        cr->set_source_rgb(0.72, 0.72, 0.72);
      } else {
        cr->set_source_rgb(0.92, 0.92, 0.92);
      }
      cr->rectangle(x, y, cell, cell);
      cr->fill();
    }
  }
}

void draw_checkmark(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, bool light_on_dark) {
  if (light_on_dark) {
    cr->set_source_rgb(1.0, 1.0, 1.0);
  } else {
    cr->set_source_rgb(0.05, 0.05, 0.05);
  }
  cr->set_line_width(std::max(1.5, std::min(w, h) / 8.0));
  cr->set_line_cap(Cairo::LINE_CAP_ROUND);
  cr->set_line_join(Cairo::LINE_JOIN_ROUND);
  const double x0 = w * 0.22;
  const double y0 = h * 0.52;
  const double x1 = w * 0.42;
  const double y1 = h * 0.72;
  const double x2 = w * 0.78;
  const double y2 = h * 0.28;
  cr->move_to(x0, y0);
  cr->line_to(x1, y1);
  cr->line_to(x2, y2);
  cr->stroke();
}

}  // namespace

ColorsPanel::ColorsPanel()
    : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4), custom_row_(Gtk::ORIENTATION_HORIZONTAL, 3) {
  set_border_width(4);

  palette_grid_.set_row_spacing(2);
  palette_grid_.set_column_spacing(2);
  palette_grid_.set_column_homogeneous(true);
  palette_grid_.set_row_homogeneous(true);

  custom_label_.set_text("Custom");
  custom_label_.set_xalign(0.0f);
  custom_label_.get_style_context()->add_class("dim-label");

  hint_.set_text("L:FG  M:BG  R:store FG");
  hint_.set_xalign(0.0f);
  hint_.set_line_wrap(true);
  hint_.set_max_width_chars(22);
  hint_.get_style_context()->add_class("dim-label");

  pack_start(palette_grid_, Gtk::PACK_SHRINK);
  pack_start(custom_label_, Gtk::PACK_SHRINK);
  pack_start(custom_row_, Gtk::PACK_SHRINK);
  pack_start(hint_, Gtk::PACK_SHRINK);

  palette_.assign(kShadePalette, kShadePalette + kCols * kRows);
  // Seed Custom with transparent so alpha remains reachable.
  custom_.push_back(Color::transparent());

  build_palette_grid();
  rebuild_custom_row();
}

void ColorsPanel::set_colors(Color fg, Color bg) {
  fg_ = fg;
  bg_ = bg;
  queue_all_draws();
}

void ColorsPanel::build_palette_grid() {
  palette_areas_.clear();
  // Column-major: each column is one hue, row 0 = lightest.
  for (int col = 0; col < kCols; ++col) {
    for (int row = 0; row < kRows; ++row) {
      const int index = col * kRows + row;
      auto* area = make_swatch(SlotKind::Palette, index, "Palette color");
      palette_grid_.attach(*area, col, row, 1, 1);
      palette_areas_.push_back(area);
    }
  }
  palette_grid_.show_all();
}

void ColorsPanel::rebuild_custom_row() {
  const std::vector<Gtk::Widget*> kids = custom_row_.get_children();
  for (Gtk::Widget* child : kids) {
    custom_row_.remove(*child);
    delete child;
  }
  custom_areas_.clear();
  add_area_ = nullptr;

  add_area_ = make_swatch(SlotKind::Add, 0, "Add custom color");
  custom_row_.pack_start(*add_area_, Gtk::PACK_SHRINK);

  for (int i = 0; i < static_cast<int>(custom_.size()); ++i) {
    const char* tip = custom_[static_cast<std::size_t>(i)].a == 0 ? "Transparent" : "Custom color";
    auto* area = make_swatch(SlotKind::Custom, i, tip);
    custom_row_.pack_start(*area, Gtk::PACK_SHRINK);
    custom_areas_.push_back(area);
  }
  custom_row_.show_all();
}

Gtk::DrawingArea* ColorsPanel::make_swatch(SlotKind kind, int index, const char* tip) {
  auto* area = Gtk::manage(new Gtk::DrawingArea());
  area->set_size_request(kSwatchW, kSwatchH);
  area->set_tooltip_text(tip);
  area->add_events(Gdk::BUTTON_PRESS_MASK);
  area->signal_draw().connect(
      [this, area, kind, index](const Cairo::RefPtr<Cairo::Context>& cr) {
        return on_swatch_draw(area, cr, kind, index);
      });
  area->signal_button_press_event().connect(
      [this, kind, index](GdkEventButton* event) { return on_swatch_press(event, kind, index); });
  return area;
}

void ColorsPanel::queue_all_draws() {
  for (auto* a : palette_areas_) {
    if (a) {
      a->queue_draw();
    }
  }
  for (auto* a : custom_areas_) {
    if (a) {
      a->queue_draw();
    }
  }
  if (add_area_) {
    add_area_->queue_draw();
  }
}

Color ColorsPanel::slot_color(SlotKind kind, int index) const {
  if (kind == SlotKind::Palette && index >= 0 && index < static_cast<int>(palette_.size())) {
    return palette_[static_cast<std::size_t>(index)];
  }
  if (kind == SlotKind::Custom && index >= 0 && index < static_cast<int>(custom_.size())) {
    return custom_[static_cast<std::size_t>(index)];
  }
  return Color::transparent();
}

bool ColorsPanel::color_matches_fg(Color c) const {
  return c == fg_;
}

bool ColorsPanel::on_swatch_draw(Gtk::DrawingArea* area, const Cairo::RefPtr<Cairo::Context>& cr,
                                 SlotKind kind, int index) {
  const int w = area->get_allocated_width();
  const int h = area->get_allocated_height();
  const double radius = 4.0;

  if (kind == SlotKind::Add) {
    path_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, radius);
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->fill_preserve();
    cr->set_source_rgb(0.35, 0.35, 0.35);
    cr->set_line_width(1.0);
    cr->stroke();
    cr->set_source_rgb(0.15, 0.15, 0.15);
    cr->set_line_width(2.0);
    cr->set_line_cap(Cairo::LINE_CAP_ROUND);
    const double cx = w / 2.0;
    const double cy = h / 2.0;
    const double arm = std::min(w, h) * 0.28;
    cr->move_to(cx - arm, cy);
    cr->line_to(cx + arm, cy);
    cr->move_to(cx, cy - arm);
    cr->line_to(cx, cy + arm);
    cr->stroke();
    return true;
  }

  const Color color = slot_color(kind, index);

  // Clip to rounded rect then fill.
  path_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, radius);
  cr->clip();

  if (color.a == 0) {
    draw_checker(cr, w, h);
  } else if (color.a < 255) {
    draw_checker(cr, w, h);
    cr->set_source_rgba(color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
    cr->paint();
  } else {
    cr->set_source_rgb(color.r / 255.0, color.g / 255.0, color.b / 255.0);
    cr->paint();
  }

  cr->reset_clip();
  path_rounded_rect(cr, 0.5, 0.5, w - 1.0, h - 1.0, radius);
  cr->set_source_rgb(0.28, 0.28, 0.28);
  cr->set_line_width(1.0);
  cr->stroke();

  if (color_matches_fg(color)) {
    draw_checkmark(cr, w, h, luminance(color) < 0.55 || color.a < 128);
  }
  return true;
}

bool ColorsPanel::on_swatch_press(GdkEventButton* event, SlotKind kind, int index) {
  if (event == nullptr) {
    return false;
  }
  if (kind == SlotKind::Add) {
    if (event->button == 1) {
      add_custom_color();
      return true;
    }
    return false;
  }

  Color* slot = nullptr;
  Gtk::DrawingArea* area = nullptr;
  if (kind == SlotKind::Palette && index >= 0 && index < static_cast<int>(palette_.size())) {
    slot = &palette_[static_cast<std::size_t>(index)];
    area = palette_areas_[static_cast<std::size_t>(index)];
  } else if (kind == SlotKind::Custom && index >= 0 && index < static_cast<int>(custom_.size())) {
    slot = &custom_[static_cast<std::size_t>(index)];
    area = custom_areas_[static_cast<std::size_t>(index)];
  } else {
    return false;
  }

  if (event->button == 3) {
    // Store current FG into this swatch (including overwriting transparent).
    *slot = fg_;
    if (area) {
      area->queue_draw();
    }
    queue_all_draws();  // refresh checkmarks
    return true;
  }
  if (!on_swatch) {
    return false;
  }
  if (event->button == 1) {
    on_swatch(*slot, (event->state & GDK_SHIFT_MASK) != 0);
    queue_all_draws();
    return true;
  }
  if (event->button == 2) {
    on_swatch(*slot, true);
    queue_all_draws();
    return true;
  }
  return false;
}

void ColorsPanel::add_custom_color() {
  if (static_cast<int>(custom_.size()) >= kMaxCustom) {
    return;
  }
  Gtk::ColorChooserDialog dialog("Add custom color");
  if (auto* top = dynamic_cast<Gtk::Window*>(get_toplevel())) {
    dialog.set_transient_for(*top);
  }
  dialog.set_use_alpha(true);
  Gdk::RGBA rgba;
  rgba.set_rgba(fg_.r / 255.0, fg_.g / 255.0, fg_.b / 255.0, fg_.a / 255.0);
  dialog.set_rgba(rgba);
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const Gdk::RGBA chosen = dialog.get_rgba();
  Color color;
  color.r = static_cast<std::uint8_t>(chosen.get_red() * 255.0 + 0.5);
  color.g = static_cast<std::uint8_t>(chosen.get_green() * 255.0 + 0.5);
  color.b = static_cast<std::uint8_t>(chosen.get_blue() * 255.0 + 0.5);
  color.a = static_cast<std::uint8_t>(chosen.get_alpha() * 255.0 + 0.5);
  custom_.push_back(color);
  rebuild_custom_row();
  if (on_swatch) {
    on_swatch(color, false);
  }
  queue_all_draws();
}

}  // namespace lundukepaint
