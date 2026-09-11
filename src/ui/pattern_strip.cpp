// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/pattern_strip.hpp"

#include <algorithm>

namespace lundukepaint {
namespace {
constexpr int kSwatch = 18;
constexpr int kCurrent = 36;
constexpr int kWell = 16;
// MacPaint used 38 patterns in two rows (19 × 2).
constexpr int kCols = 19;
}  // namespace

PatternStrip::PatternStrip() : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4) {
  set_border_width(3);
  set_size_request(-1, 48);
  get_style_context()->add_class("toolbar");

  current_.set_size_request(kCurrent, kCurrent);
  current_.set_tooltip_text("Current pattern");
  current_.set_valign(Gtk::ALIGN_CENTER);
  current_.signal_draw().connect(sigc::mem_fun(*this, &PatternStrip::on_current_draw));
  pack_start(current_, Gtk::PACK_SHRINK);

  fg_well_.set_size_request(kWell, kWell);
  bg_well_.set_size_request(kWell, kWell);
  fg_well_.set_halign(Gtk::ALIGN_START);
  bg_well_.set_halign(Gtk::ALIGN_END);
  fg_well_.set_valign(Gtk::ALIGN_START);
  bg_well_.set_valign(Gtk::ALIGN_END);
  fg_well_.set_margin_end(6);
  bg_well_.set_margin_start(6);
  fg_well_.set_tooltip_text("Foreground");
  bg_well_.set_tooltip_text("Background");
  fg_well_.add_events(Gdk::BUTTON_PRESS_MASK);
  bg_well_.add_events(Gdk::BUTTON_PRESS_MASK);
  fg_well_.signal_draw().connect(sigc::mem_fun(*this, &PatternStrip::on_fg_draw));
  bg_well_.signal_draw().connect(sigc::mem_fun(*this, &PatternStrip::on_bg_draw));
  fg_well_.signal_button_press_event().connect(sigc::mem_fun(*this, &PatternStrip::on_fg_press));
  bg_well_.signal_button_press_event().connect(sigc::mem_fun(*this, &PatternStrip::on_bg_press));

  // Overlapping FG (front-left) / BG (back-right) like MacPaint.
  auto* well_box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 0));
  well_box->set_size_request(kWell + 10, kWell + 10);
  well_box->set_valign(Gtk::ALIGN_CENTER);
  auto* overlay = Gtk::manage(new Gtk::Fixed());
  overlay->set_size_request(kWell + 10, kWell + 10);
  overlay->put(bg_well_, 8, 8);
  overlay->put(fg_well_, 0, 0);
  well_box->pack_start(*overlay, Gtk::PACK_SHRINK);
  pack_start(*well_box, Gtk::PACK_SHRINK);

  swatches_.set_row_spacing(1);
  swatches_.set_column_spacing(1);
  swatches_.set_column_homogeneous(true);
  swatches_.set_row_homogeneous(true);
  for (int i = 0; i < kPatternCount; ++i) {
    auto* area = Gtk::manage(new Gtk::DrawingArea());
    area->set_size_request(kSwatch, kSwatch);
    area->set_tooltip_text("Pattern");
    area->add_events(Gdk::BUTTON_PRESS_MASK);
    const int index = i;
    area->signal_draw().connect(
        [this, area, index](const Cairo::RefPtr<Cairo::Context>& cr) {
          return on_swatch_draw(area, cr, index);
        });
    area->signal_button_press_event().connect(
        [this, index](GdkEventButton* event) { return on_swatch_press(event, index); });
    const int row = i / kCols;
    const int col = i % kCols;
    swatches_.attach(*area, col, row, 1, 1);
    areas_.push_back(area);
  }
  scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_NEVER);
  scroll_.set_hexpand(true);
  scroll_.add(swatches_);
  pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);
}

void PatternStrip::set_colors(Color fg, Color bg) {
  fg_ = fg;
  bg_ = bg;
  current_.queue_draw();
  fg_well_.queue_draw();
  bg_well_.queue_draw();
  for (auto* area : areas_) {
    area->queue_draw();
  }
}

void PatternStrip::set_pattern_index(int index) {
  pattern_index_ = clamp_pattern_index(index);
  current_.queue_draw();
  for (auto* area : areas_) {
    area->queue_draw();
  }
}

void PatternStrip::draw_pattern(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h,
                                int index) const {
  const Pattern& pat = pattern_at(index);
  const int cell = std::max(1, std::min(w, h) / 8);
  const int ox = (w - cell * 8) / 2;
  const int oy = (h - cell * 8) / 2;
  for (int py = 0; py < 8; ++py) {
    for (int px = 0; px < 8; ++px) {
      const Color c = pat.color_at(px, py, fg_, bg_);
      if (c.a == 0) {
        cr->set_source_rgb(0.85, 0.85, 0.85);
      } else {
        cr->set_source_rgba(c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
      }
      cr->rectangle(ox + px * cell, oy + py * cell, cell, cell);
      cr->fill();
    }
  }
  cr->set_source_rgb(0.2, 0.2, 0.2);
  cr->rectangle(0.5, 0.5, w - 1.0, h - 1.0);
  cr->set_line_width(1.0);
  cr->stroke();
}

void PatternStrip::draw_well(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h, Color c) const {
  cr->set_source_rgb(0.25, 0.25, 0.25);
  cr->rectangle(0.5, 0.5, w - 1.0, h - 1.0);
  cr->stroke();
  if (c.a == 0) {
    const int cell = 4;
    for (int y = 1; y < h - 1; y += cell) {
      for (int x = 1; x < w - 1; x += cell) {
        const bool dark = ((x / cell) + (y / cell)) & 1;
        cr->set_source_rgb(dark ? 0.62 : 0.82, dark ? 0.62 : 0.82, dark ? 0.62 : 0.82);
        cr->rectangle(x, y, std::min(cell, w - 1 - x), std::min(cell, h - 1 - y));
        cr->fill();
      }
    }
  } else {
    cr->set_source_rgba(c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
    cr->rectangle(1, 1, w - 2, h - 2);
    cr->fill();
  }
}

bool PatternStrip::on_current_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  draw_pattern(cr, current_.get_allocated_width(), current_.get_allocated_height(), pattern_index_);
  return true;
}

bool PatternStrip::on_fg_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  draw_well(cr, fg_well_.get_allocated_width(), fg_well_.get_allocated_height(), fg_);
  return true;
}

bool PatternStrip::on_bg_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  draw_well(cr, bg_well_.get_allocated_width(), bg_well_.get_allocated_height(), bg_);
  return true;
}

bool PatternStrip::on_fg_press(GdkEventButton* event) {
  if (event == nullptr || !on_well_clicked) {
    return false;
  }
  if (event->button == 2 && on_transparent) {
    on_transparent(false);
    return true;
  }
  on_well_clicked(false);
  return true;
}

bool PatternStrip::on_bg_press(GdkEventButton* event) {
  if (event == nullptr || !on_well_clicked) {
    return false;
  }
  if (event->button == 2 && on_transparent) {
    on_transparent(true);
    return true;
  }
  on_well_clicked(true);
  return true;
}

bool PatternStrip::on_swatch_draw(Gtk::DrawingArea* area, const Cairo::RefPtr<Cairo::Context>& cr,
                                 int index) {
  const int w = area->get_allocated_width();
  const int h = area->get_allocated_height();
  draw_pattern(cr, w, h, index);
  if (index == pattern_index_) {
    cr->set_source_rgb(0.15, 0.35, 0.75);
    cr->rectangle(1.5, 1.5, w - 3.0, h - 3.0);
    cr->set_line_width(2.0);
    cr->stroke();
  }
  return true;
}

bool PatternStrip::on_swatch_press(GdkEventButton* event, int index) {
  if (event == nullptr || event->button != 1) {
    return false;
  }
  set_pattern_index(index);
  if (on_pattern_chosen) {
    on_pattern_chosen(pattern_index_);
  }
  return true;
}

}  // namespace lundukepaint
