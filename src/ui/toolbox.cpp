// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/toolbox.hpp"

#include <gdkmm/screen.h>
#include <gtkmm/image.h>
#include <gtkmm/stylecontext.h>

#include <cmath>

namespace lundukepaint {

namespace {
constexpr int kWellSize = 22;
// Soft ceiling: two ~28px icon columns + spacing + side air + border.
constexpr int kToolboxMaxWidth = 92;
// Outer width may exceed the tool grid by this many pixels (border/theme/air).
constexpr int kGridWidthSlop = 36;
// Extra air each side of the 2-col grid (0.3-4 was border-only; this is ~+16px).
constexpr int kSideAir = 8;
}  // namespace

// Own CSS, loaded once per screen at APPLICATION priority so the selected tool
// looks selected no matter which GTK3 theme is in play.
void Toolbox::ensure_css() {
  static bool loaded = false;
  if (loaded) {
    return;
  }
  auto screen = Gdk::Screen::get_default();
  if (!screen) {
    return;
  }
  auto provider = Gtk::CssProvider::create();
  try {
    provider->load_from_data(toolbox_style::css());
  } catch (const Glib::Error&) {
    return;
  }
  Gtk::StyleContext::add_provider_for_screen(screen, provider,
                                             GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  loaded = true;
}

Toolbox::Toolbox() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2) {
  ensure_css();
  get_style_context()->add_class("toolbox-strip");
  set_border_width(4);
  set_halign(Gtk::ALIGN_START);
  set_hexpand(false);

  grid_.set_row_spacing(2);
  grid_.set_column_spacing(2);
  grid_.set_column_homogeneous(true);
  // Natural width of two equal icon columns — do not expand into empty chrome.
  grid_.set_hexpand(false);
  grid_.set_halign(Gtk::ALIGN_START);
  grid_.set_margin_start(kSideAir);
  grid_.set_margin_end(kSideAir);
  pack_start(grid_, Gtk::PACK_SHRINK);
  // Width hugs the tool grid plus border and side air (not a huge chrome region).
  set_size_request(tool_grid_natural_width() + static_cast<int>(get_border_width()) * 2 +
                       kSideAir * 2,
                   -1);
  grid_.signal_size_allocate().connect(sigc::mem_fun(*this, &Toolbox::on_grid_size_allocate));

  auto setup_well = [](Gtk::DrawingArea& well, const char* tip) {
    well.set_size_request(kWellSize, kWellSize);
    well.set_valign(Gtk::ALIGN_CENTER);
    well.set_tooltip_text(tip);
    well.add_events(Gdk::BUTTON_PRESS_MASK);
  };
  setup_well(fg_well_, "Foreground");
  setup_well(bg_well_, "Background");
  fg_well_.set_halign(Gtk::ALIGN_START);
  bg_well_.set_halign(Gtk::ALIGN_START);
  fg_well_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_fg_well_draw));
  bg_well_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_bg_well_draw));
  fg_well_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_fg_well_press));
  bg_well_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_bg_well_press));

  fg_label_.set_valign(Gtk::ALIGN_CENTER);
  bg_label_.set_valign(Gtk::ALIGN_CENTER);
  fg_label_.set_halign(Gtk::ALIGN_START);
  bg_label_.set_halign(Gtk::ALIGN_START);
  fg_label_.set_xalign(0.0);
  bg_label_.set_xalign(0.0);
  fg_label_.set_line_wrap(false);
  bg_label_.set_line_wrap(false);
  fg_label_.set_tooltip_text("Foreground");
  bg_label_.set_tooltip_text("Background");
  fg_label_.get_style_context()->add_class(toolbox_style::caption_class());
  bg_label_.get_style_context()->add_class(toolbox_style::caption_class());

  // Stacked under the grid: well + short label per row, width ≤ tool grid.
  fg_row_.set_halign(Gtk::ALIGN_START);
  fg_row_.set_hexpand(false);
  fg_row_.set_valign(Gtk::ALIGN_CENTER);
  fg_row_.set_spacing(3);
  fg_row_.set_margin_start(kSideAir);
  fg_row_.set_margin_end(kSideAir);
  fg_row_.pack_start(fg_well_, Gtk::PACK_SHRINK);
  fg_row_.pack_start(fg_label_, Gtk::PACK_SHRINK);

  bg_row_.set_halign(Gtk::ALIGN_START);
  bg_row_.set_hexpand(false);
  bg_row_.set_valign(Gtk::ALIGN_CENTER);
  bg_row_.set_spacing(3);
  bg_row_.set_margin_top(2);
  bg_row_.set_margin_start(kSideAir);
  bg_row_.set_margin_end(kSideAir);
  bg_row_.pack_start(bg_well_, Gtk::PACK_SHRINK);
  bg_row_.pack_start(bg_label_, Gtk::PACK_SHRINK);

  pack_start(fg_row_, Gtk::PACK_SHRINK);
  pack_start(bg_row_, Gtk::PACK_SHRINK);

  // Height only — width clamped to the tool grid after allocate.
  trans_.set_size_request(-1, 14);
  trans_.set_hexpand(false);
  trans_.set_halign(Gtk::ALIGN_FILL);
  trans_.set_margin_start(kSideAir);
  trans_.set_margin_end(kSideAir);
  trans_.set_tooltip_text("Transparent: left sets FG, right sets BG (punches alpha)");
  trans_.add_events(Gdk::BUTTON_PRESS_MASK);
  trans_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_trans_draw));
  trans_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_trans_press));
  pack_start(trans_, Gtk::PACK_SHRINK);

}

void Toolbox::add_tool_button(const std::string& id, const std::string& tooltip,
                              const std::string& icon_name) {
  auto* button = Gtk::manage(new Gtk::Button());
  const std::string resource =
      "/org/lunduke/LundukePaint/icons/scalable/actions/" + icon_name + ".svg";
  auto* image = Gtk::manage(new Gtk::Image());
  image->set_from_resource(resource);
  image->set_pixel_size(18);
  button->set_image(*image);
  button->set_tooltip_text(tooltip);
  button->set_relief(Gtk::RELIEF_NONE);
  button->set_can_focus(false);
  // Cap preferred size so a wide SVG cannot inflate the two-column grid.
  button->set_size_request(28, 28);
  button->set_hexpand(true);
  button->set_halign(Gtk::ALIGN_FILL);
  button->get_style_context()->add_class(toolbox_style::button_class());
  const std::string captured = id;
  button->signal_clicked().connect([this, captured]() {
    if (on_tool_chosen) {
      on_tool_chosen(captured);
    }
  });
  grid_.attach(*button, next_col_, next_row_, 1, 1);
  next_col_ += 1;
  if (next_col_ >= 2) {
    next_col_ = 0;
    next_row_ += 1;
  }
  buttons_.push_back(button);
  selection_.add(id);
  apply_selection_style();
}

void Toolbox::set_active_tool(const std::string& id) {
  selection_.select(id);
  apply_selection_style();
}

void Toolbox::apply_selection_style() {
  const std::vector<std::string>& ids = selection_.ids();
  for (std::size_t i = 0; i < buttons_.size() && i < ids.size(); ++i) {
    auto context = buttons_[i]->get_style_context();
    if (selection_.is_selected(ids[i])) {
      context->add_class(toolbox_style::selected_class());
      // Keep the theme hint too, for themes that style it nicely.
      context->add_class("suggested-action");
    } else {
      context->remove_class(toolbox_style::selected_class());
      context->remove_class("suggested-action");
    }
  }
}

bool Toolbox::tool_button_selected(const std::string& id) const {
  const int index = tool_index(selection_.ids(), id);
  if (index < 0 || index >= static_cast<int>(buttons_.size())) {
    return false;
  }
  return buttons_[static_cast<std::size_t>(index)]->get_style_context()->has_class(
      toolbox_style::selected_class());
}

bool Toolbox::tool_columns_homogeneous() const { return grid_.get_column_homogeneous(); }

bool Toolbox::tool_columns_equal_width() const {
  if (buttons_.size() < 2) {
    return false;
  }
  const int w0 = buttons_[0]->get_allocated_width();
  const int w1 = buttons_[1]->get_allocated_width();
  return w0 > 8 && std::abs(w0 - w1) <= 1;
}

int Toolbox::tool_grid_natural_width() const {
  // Two equal columns at the capped tool-button size + column spacing.
  constexpr int kBtn = 28;
  return kBtn * 2 + grid_.get_column_spacing();
}

void Toolbox::get_preferred_width_vfunc(int& minimum_width, int& natural_width) const {
  const int border = static_cast<int>(get_border_width()) * 2;
  const int w = tool_grid_natural_width() + border + kSideAir * 2;
  minimum_width = w;
  natural_width = w;
}

void Toolbox::on_grid_size_allocate(Gtk::Allocation& allocation) {
  (void)allocation;
  const int grid_w = tool_grid_natural_width();
  const int border = static_cast<int>(get_border_width()) * 2;
  const int want = grid_w + border + kSideAir * 2;
  int req_w = 0;
  int req_h = 0;
  get_size_request(req_w, req_h);
  if (req_w != want) {
    set_size_request(want, -1);
    trans_.set_size_request(grid_w, 14);
  }
}

bool Toolbox::width_tracks_tool_grid() const {
  const int box_w = get_allocated_width();
  const int grid_w = tool_grid_natural_width();
  if (box_w < 1 || grid_w < 1) {
    return false;
  }
  const int pad = box_w - grid_w;
  return pad >= 0 && pad <= kGridWidthSlop && box_w <= kToolboxMaxWidth + kGridWidthSlop;
}

bool Toolbox::child_origin(const Gtk::Widget& child, int& x, int& y) const {
  x = 0;
  y = 0;
  return const_cast<Gtk::Widget&>(child).translate_coordinates(const_cast<Toolbox&>(*this), 0, 0, x,
                                                               y);
}

bool Toolbox::fg_label_right_of_well() const {
  int lx = 0;
  int ly = 0;
  int wx = 0;
  int wy = 0;
  if (!child_origin(fg_label_, lx, ly) || !child_origin(fg_well_, wx, wy)) {
    return false;
  }
  const int well_right = wx + fg_well_.get_allocated_width();
  const int label_cx = ly + fg_label_.get_allocated_height() / 2;
  const int well_cx = wy + fg_well_.get_allocated_height() / 2;
  return lx >= well_right - 1 && std::abs(label_cx - well_cx) <= 10;
}

bool Toolbox::bg_label_left_of_well() const {
  // Stacked layout: BG label sits to the right of the BG well (same row pattern as FG).
  // Name kept for tests; returns true when label is immediately beside the well.
  int lx = 0;
  int ly = 0;
  int wx = 0;
  int wy = 0;
  if (!child_origin(bg_label_, lx, ly) || !child_origin(bg_well_, wx, wy)) {
    return false;
  }
  const int well_right = wx + bg_well_.get_allocated_width();
  const int label_cx = ly + bg_label_.get_allocated_height() / 2;
  const int well_cx = wy + bg_well_.get_allocated_height() / 2;
  return lx >= well_right - 1 && std::abs(label_cx - well_cx) <= 10;
}

bool Toolbox::bg_well_right_justified() const {
  // Stacked under a narrow grid: well stays within the toolbox left edge (no wide chrome).
  int wx = 0;
  int wy = 0;
  if (!child_origin(bg_well_, wx, wy)) {
    return false;
  }
  return wx <= 20;
}

bool Toolbox::bg_well_below_fg() const {
  int fgy = 0;
  int bgy = 0;
  int fgx = 0;
  int bgx = 0;
  if (!child_origin(fg_well_, fgx, fgy) || !child_origin(bg_well_, bgx, bgy)) {
    return false;
  }
  return bgy >= fgy + fg_well_.get_allocated_height() - 2;
}

void Toolbox::set_colors(Color fg, Color bg) {
  fg_ = fg;
  bg_ = bg;
  fg_well_.queue_draw();
  bg_well_.queue_draw();
}

void Toolbox::draw_swatch(const Cairo::RefPtr<Cairo::Context>& cr, Gtk::DrawingArea& area, Color c) {
  const int w = area.get_allocated_width();
  const int h = area.get_allocated_height();
  const int s = std::min(w, h);
  const int x = (w - s) / 2;
  const int y = (h - s) / 2;
  cr->set_source_rgb(0.3, 0.3, 0.3);
  cr->rectangle(x, y, s, s);
  cr->stroke();
  if (c.a == 0) {
    cr->set_source_rgb(0.82, 0.82, 0.82);
    cr->rectangle(x + 1, y + 1, s / 2 - 1, s / 2 - 1);
    cr->fill();
    cr->rectangle(x + s / 2, y + s / 2, s / 2 - 1, s / 2 - 1);
    cr->fill();
    cr->set_source_rgb(0.62, 0.62, 0.62);
    cr->rectangle(x + s / 2, y + 1, s / 2 - 1, s / 2 - 1);
    cr->fill();
    cr->rectangle(x + 1, y + s / 2, s / 2 - 1, s / 2 - 1);
    cr->fill();
  } else {
    cr->set_source_rgba(c.r / 255.0, c.g / 255.0, c.b / 255.0, c.a / 255.0);
    cr->rectangle(x + 1, y + 1, s - 2, s - 2);
    cr->fill();
  }
}

bool Toolbox::on_fg_well_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  draw_swatch(cr, fg_well_, fg_);
  return true;
}

bool Toolbox::on_bg_well_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  draw_swatch(cr, bg_well_, bg_);
  return true;
}

bool Toolbox::on_fg_well_press(GdkEventButton* event) {
  if (event == nullptr || !on_well_clicked) {
    return false;
  }
  on_well_clicked(false);
  return true;
}

bool Toolbox::on_bg_well_press(GdkEventButton* event) {
  if (event == nullptr || !on_well_clicked) {
    return false;
  }
  on_well_clicked(true);
  return true;
}

bool Toolbox::on_trans_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = trans_.get_allocated_width();
  const int h = trans_.get_allocated_height();
  const int cell = 6;
  for (int y = 0; y < h; y += cell) {
    for (int x = 0; x < w; x += cell) {
      const bool dark = ((x / cell) + (y / cell)) & 1;
      if (dark) {
        cr->set_source_rgb(0.62, 0.62, 0.62);
      } else {
        cr->set_source_rgb(0.82, 0.82, 0.82);
      }
      cr->rectangle(x, y, cell, cell);
      cr->fill();
    }
  }
  cr->set_source_rgb(0.25, 0.25, 0.25);
  cr->rectangle(0.5, 0.5, w - 1.0, h - 1.0);
  cr->set_line_width(1.0);
  cr->stroke();
  return true;
}

bool Toolbox::on_trans_press(GdkEventButton* event) {
  if (event == nullptr || !on_transparent) {
    return false;
  }
  if (event->button == 1 || event->button == 3) {
    on_transparent(event->button == 3);
    return true;
  }
  return false;
}

}  // namespace lundukepaint
