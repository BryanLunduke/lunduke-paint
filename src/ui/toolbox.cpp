// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/toolbox.hpp"

#include "app/live_edit.hpp"
#include "ui/color_well.hpp"
#include "ui/symbolic_icon.hpp"

#include "raster/brush_tip.hpp"

#include <gdkmm/pixbuf.h>
#include <gdkmm/screen.h>
#include <gtkmm/container.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include <gtkmm/stylecontext.h>

#include <string>

#include <cmath>
#include <cstring>

namespace lundukepaint {

namespace {
// Classic MacPaint rail, from acd6af9 ("MacPaint brush rail", 0.5-4) and
// unchanged through tags v0.9-2 .. v0.9-5: two 28 px columns, the grid's
// 2 px gap, a 4 px border on each side, and 8 px of air on each side.
// 28 * 2 + 2 + 8 + 16 = 82. Commit 37b4520 (v0.9-6) replaced this with a
// 220 px minimum; b8a8e37 (v0.9-7) then stretched the pickers to that width.
constexpr int kGridWidthSlop = 36;
constexpr int kSideAir = 8;
constexpr int kLineChoices[5] = {1, 2, 3, 5, 8};
constexpr int kSprayChoices[5] = {4, 8, 12, 16, 24};
constexpr int kBrushCols = 4;
constexpr int kBrushRows = 4;


void fill_picker_bg(Gtk::Widget& host, const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
  // Match the toolbox/tool-rail theme background (not a hardcoded gray that
  // reads slightly darker than the strip under Adwaita / Clearlooks).
  Gdk::RGBA bg;
  if (host.get_style_context()->lookup_color("theme_bg_color", bg)) {
    cr->set_source_rgba(bg.get_red(), bg.get_green(), bg.get_blue(), bg.get_alpha());
  } else {
    host.get_style_context()->render_background(cr, 0, 0, w, h);
    return;
  }
  cr->rectangle(0, 0, w, h);
  cr->fill();
}

bool uses_line_width(const std::string& id) {
  return id == "pencil" || id == "eraser" || id == "line" || id == "rectangle" ||
         id == "rectangle-fill" || id == "rounded-rect" || id == "rounded-rect-fill" ||
         id == "ellipse" || id == "ellipse-fill" || id == "freeform" || id == "freeform-fill" ||
         id == "polygon" || id == "polygon-fill" || id == "polyline" || id == "curve";
}

// Tooltip plus the matching accessible name. Realized again on map so a
// theme that rebuilds the ATK object still exposes the name.
void describe_control(Gtk::Widget& widget, const char* text) {
  const std::string copy(text);
  widget.set_tooltip_text(copy);
  widget.set_has_tooltip(true);
  auto apply = [&widget, copy]() {
    auto accessible = widget.get_accessible();
    if (accessible) {
      accessible->set_name(copy);
    }
  };
  apply();
  widget.signal_map().connect(apply);
}
}  // namespace

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
  grid_.set_hexpand(false);
  grid_.set_halign(Gtk::ALIGN_CENTER);
  grid_.set_size_request(tool_grid_natural_width(), -1);
  grid_.set_margin_start(kSideAir);
  grid_.set_margin_end(kSideAir);
  rail_.pack_start(grid_, Gtk::PACK_SHRINK);
  grid_.signal_size_allocate().connect(sigc::mem_fun(*this, &Toolbox::on_grid_size_allocate));

  // Little air + hairline between the tool grid and the stroke/brush pickers.
  auto* rail_sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_HORIZONTAL));
  rail_sep->set_margin_start(kSideAir);
  rail_sep->set_margin_end(kSideAir);
  rail_sep->set_margin_top(6);
  rail_sep->set_margin_bottom(6);
  rail_.pack_start(*rail_sep, Gtk::PACK_SHRINK);

  options_stack_.set_transition_type(Gtk::STACK_TRANSITION_TYPE_NONE);
  options_stack_.set_margin_start(kSideAir);
  options_stack_.set_margin_end(kSideAir);
  options_stack_.set_margin_top(0);
  options_stack_.set_hexpand(false);
  options_stack_.set_halign(Gtk::ALIGN_CENTER);

  const int picker_w = tool_grid_natural_width();

  // MacPaint-style line-width selector. Rows are as wide as the two tool
  // columns, not the window.
  line_widths_.set_hexpand(false);
  line_widths_.set_halign(Gtk::ALIGN_CENTER);
  line_widths_.set_size_request(picker_w, 72);
  line_widths_.set_tooltip_text("Line width");
  line_widths_.add_events(Gdk::BUTTON_PRESS_MASK);
  line_widths_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_line_width_draw));
  line_widths_.signal_button_press_event().connect(
      sigc::mem_fun(*this, &Toolbox::on_line_width_press));
  options_stack_.add(line_widths_, "line", "Line width");

  // MacPaint-style brush tip grid (4×4).
  brush_tips_.set_hexpand(false);
  brush_tips_.set_halign(Gtk::ALIGN_CENTER);
  brush_tips_.set_size_request(picker_w, picker_w);
  brush_tips_.set_tooltip_text("Brush shape");
  brush_tips_.add_events(Gdk::BUTTON_PRESS_MASK);
  brush_tips_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_brush_tips_draw));
  brush_tips_.signal_button_press_event().connect(
      sigc::mem_fun(*this, &Toolbox::on_brush_tips_press));
  options_stack_.add(brush_tips_, "brush", "Brush shape");

  // Compact spray radius rows (same visual language as line widths).
  spray_radii_.set_hexpand(false);
  spray_radii_.set_halign(Gtk::ALIGN_CENTER);
  spray_radii_.set_size_request(picker_w, 72);
  spray_radii_.set_tooltip_text("Spray radius");
  spray_radii_.add_events(Gdk::BUTTON_PRESS_MASK);
  spray_radii_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_spray_draw));
  spray_radii_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_spray_press));
  options_stack_.add(spray_radii_, "spray", "Spray radius");

  empty_options_.set_size_request(picker_w, 8);
  options_stack_.add(empty_options_, "none", "None");

  rail_.pack_start(options_stack_, Gtk::PACK_SHRINK);
  options_stack_.set_visible_child("line");

  tool_options_.set_margin_start(kSideAir);
  tool_options_.set_margin_end(kSideAir);
  tool_options_.set_hexpand(false);
  tool_options_.set_halign(Gtk::ALIGN_CENTER);
  tool_options_.set_size_request(picker_w, -1);
  tool_options_.get_style_context()->add_class("toolbox-options");
  tool_options_.set_no_show_all(true);
  tool_options_.hide();
  rail_.pack_start(tool_options_, Gtk::PACK_SHRINK);

  scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll_.set_propagate_natural_width(true);
  scroll_.set_propagate_natural_height(false);
  scroll_.set_min_content_width(picker_w + kSideAir * 2);
  scroll_.set_max_content_width(picker_w + kSideAir * 2);
  scroll_.set_min_content_height(72);
  scroll_.set_shadow_type(Gtk::SHADOW_NONE);
  scroll_.set_hexpand(false);
  scroll_.set_vexpand(true);
  rail_.set_hexpand(false);
  rail_.set_halign(Gtk::ALIGN_START);
  scroll_.add(rail_);
  pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);

  // Wells stay outside the scrolled tool list so they remain visible.
  // Foreground and background are separate widgets so each has its own
  // tooltip. They overlap the same way the single well used to: the front
  // square is added last and wins the overlap.
  wells_fixed_.set_size_request(ColorWellGeom::kWidth, ColorWellGeom::kHeight);
  wells_fixed_.set_hexpand(false);
  wells_fixed_.set_halign(Gtk::ALIGN_CENTER);
  wells_fixed_.set_valign(Gtk::ALIGN_CENTER);
  bg_well_.set_size_request(ColorWellGeom::kBg, ColorWellGeom::kBg);
  bg_well_.set_hexpand(false);
  bg_well_.set_can_focus(false);
  describe_control(bg_well_, kBackgroundWellTooltip);
  bg_well_.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::ENTER_NOTIFY_MASK);
  bg_well_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_bg_draw));
  bg_well_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_bg_press));
  fg_well_.set_size_request(ColorWellGeom::kFg, ColorWellGeom::kFg);
  fg_well_.set_hexpand(false);
  fg_well_.set_can_focus(false);
  describe_control(fg_well_, kForegroundWellTooltip);
  fg_well_.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::ENTER_NOTIFY_MASK);
  fg_well_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_fg_draw));
  fg_well_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_fg_press));
  wells_fixed_.put(bg_well_, ColorWellGeom::kBgX, ColorWellGeom::kBgY);
  wells_fixed_.put(fg_well_, ColorWellGeom::kFgX, ColorWellGeom::kFgY);
  swap_colors_.set_size_request(22, 22);
  swap_colors_.set_can_focus(false);
  describe_control(swap_colors_, kSwapColorsTooltip);
  swap_colors_.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::ENTER_NOTIFY_MASK);
  swap_colors_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_swap_draw));
  swap_colors_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_swap_press));
  reset_colors_.set_size_request(22, 22);
  reset_colors_.set_can_focus(false);
  describe_control(reset_colors_, kResetColorsTooltip);
  reset_colors_.add_events(Gdk::BUTTON_PRESS_MASK | Gdk::ENTER_NOTIFY_MASK);
  reset_colors_.signal_draw().connect(sigc::mem_fun(*this, &Toolbox::on_reset_draw));
  reset_colors_.signal_button_press_event().connect(sigc::mem_fun(*this, &Toolbox::on_reset_press));
  // Swap and reset stack beside the wells so the cluster fits the 82 px
  // rail. A single row of 48 + 22 + 22 does not.
  auto* well_actions = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2));
  well_actions->set_halign(Gtk::ALIGN_CENTER);
  well_actions->set_valign(Gtk::ALIGN_CENTER);
  well_actions->pack_start(swap_colors_, Gtk::PACK_SHRINK);
  well_actions->pack_start(reset_colors_, Gtk::PACK_SHRINK);
  color_row_.set_spacing(2);
  color_row_.set_margin_top(4);
  color_row_.set_halign(Gtk::ALIGN_CENTER);
  color_row_.set_hexpand(false);
  color_row_.pack_start(wells_fixed_, Gtk::PACK_SHRINK);
  color_row_.pack_start(*well_actions, Gtk::PACK_SHRINK);
  pack_start(color_row_, Gtk::PACK_SHRINK);
  set_size_request(classic_outer_width(), -1);
  set_vexpand(true);
}

void Toolbox::add_tool_button(const std::string& id, const std::string& tooltip,
                              const std::string& icon_name) {
  auto* button = Gtk::manage(new Gtk::Button());
  const std::string resource =
      "/org/lunduke/LundukePaint/icons/scalable/actions/" + icon_name + ".svg";
  auto* image = Gtk::manage(new Gtk::Image());
  try {
    // Bake theme_fg into the SVG (currentColor) — pixbuf decode ignores CSS color.
    auto pixbuf = symbolic_icon::load_from_resource(*this, resource, 18);
    image->set(pixbuf);
  } catch (const Glib::Error&) {
    image->set_from_resource(resource);
    image->set_pixel_size(18);
  } catch (const std::exception&) {
    image->set_from_resource(resource);
    image->set_pixel_size(18);
  }
  button->set_image(*image);
  button->set_tooltip_text(tooltip);
  button->set_relief(Gtk::RELIEF_NONE);
  button->set_can_focus(false);
  button->set_size_request(28, 28);
  button->set_hexpand(false);
  button->set_halign(Gtk::ALIGN_CENTER);
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
  show_options_for_tool(id);
}

void Toolbox::show_options_for_tool(const std::string& id) {
  const bool picker = id == "brush" || id == "spray" || uses_line_width(id);
  if (id == "brush") {
    options_stack_.set_visible_child("brush");
  } else if (id == "spray") {
    options_stack_.set_visible_child("spray");
  } else if (uses_line_width(id)) {
    options_stack_.set_visible_child("line");
  } else {
    options_stack_.set_visible_child("none");
  }
  // Hand and picker have no picker. Their empty page stays hidden so the
  // rail does not keep an 8px blank under the tool grid.
  options_stack_.set_visible(picker);
}

namespace {

void fit_option_labels(Gtk::Widget& widget) {
  if (auto* label = dynamic_cast<Gtk::Label*>(&widget)) {
    if (!label->get_line_wrap() && label->get_ellipsize() == Pango::ELLIPSIZE_NONE) {
      label->set_ellipsize(Pango::ELLIPSIZE_END);
      label->set_max_width_chars(8);
      label->set_xalign(0.0f);
    }
  }
  auto* container = dynamic_cast<Gtk::Container*>(&widget);
  if (container == nullptr) {
    return;
  }
  for (Gtk::Widget* child : container->get_children()) {
    if (child != nullptr) {
      fit_option_labels(*child);
    }
  }
}

}  // namespace

void Toolbox::set_tool_options(Gtk::Widget* widget) {
  if (hosted_options_ == widget) {
    if (widget != nullptr) {
      tool_options_.show();
      widget->show_all();
    }
    return;
  }
  if (hosted_options_ != nullptr && hosted_options_->get_parent() == &tool_options_) {
    tool_options_.remove(*hosted_options_);
  }
  hosted_options_ = widget;
  if (widget == nullptr) {
    tool_options_.hide();
    return;
  }
  if (widget->get_parent() != nullptr && widget->get_parent() != &tool_options_) {
    widget->get_parent()->remove(*widget);
  }
  widget->set_hexpand(false);
  widget->set_halign(Gtk::ALIGN_FILL);
  fit_option_labels(*widget);
  tool_options_.pack_start(*widget, Gtk::PACK_SHRINK);
  widget->show_all();
  tool_options_.show();
}

void Toolbox::set_line_width(int width) {
  if (width < 1) {
    width = 1;
  }
  line_width_ = width;
  line_widths_.queue_draw();
}

void Toolbox::set_brush_tip(int index) {
  brush_tip_ = clamp_brush_tip_index(index);
  brush_tips_.queue_draw();
}

void Toolbox::set_spray_radius(int radius) {
  if (radius < 1) {
    radius = 1;
  }
  spray_radius_ = radius;
  spray_radii_.queue_draw();
}

void Toolbox::apply_selection_style() {
  const std::vector<std::string>& ids = selection_.ids();
  for (std::size_t i = 0; i < buttons_.size() && i < ids.size(); ++i) {
    auto context = buttons_[i]->get_style_context();
    if (selection_.is_selected(ids[i])) {
      context->add_class(toolbox_style::selected_class());
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
  constexpr int kBtn = 28;
  return kBtn * 2 + grid_.get_column_spacing();
}

int Toolbox::classic_outer_width() const {
  const int border = static_cast<int>(get_border_width()) * 2;
  return tool_grid_natural_width() + border + kSideAir * 2;
}

void Toolbox::get_preferred_width_vfunc(int& minimum_width, int& natural_width) const {
  const int w = classic_outer_width();
  minimum_width = w;
  natural_width = w;
}

int Toolbox::tool_button_width() const {
  if (buttons_.empty() || buttons_[0] == nullptr) {
    return 0;
  }
  return buttons_[0]->get_allocated_width();
}

int Toolbox::line_width_picker_width() const { return line_widths_.get_allocated_width(); }

void Toolbox::set_colors(Color fg, Color bg) {
  fg_ = fg;
  bg_ = bg;
  fg_well_.queue_draw();
  bg_well_.queue_draw();
}

void Toolbox::size_option_panels() {
  const int picker_w = tool_grid_natural_width();
  line_widths_.set_size_request(picker_w, 72);
  brush_tips_.set_size_request(picker_w, picker_w);
  spray_radii_.set_size_request(picker_w, 72);
  empty_options_.set_size_request(picker_w, 8);
  tool_options_.set_size_request(picker_w, -1);
}

void Toolbox::on_grid_size_allocate(Gtk::Allocation& allocation) {
  (void)allocation;
  const int want = classic_outer_width();
  int req_w = 0;
  int req_h = 0;
  get_size_request(req_w, req_h);
  if (req_w != want) {
    set_size_request(want, -1);
  }
  size_option_panels();
}

bool Toolbox::width_tracks_tool_grid() const {
  const int alloc = grid_.get_allocated_width();
  const int grid_w = tool_grid_natural_width();
  if (alloc < 1 || grid_w < 1) {
    return false;
  }
  const int pad = alloc - grid_w;
  return pad >= -1 && pad <= kGridWidthSlop;
}

int Toolbox::width_at_y(double y) const {
  const int h = line_widths_.get_allocated_height();
  const int row = std::max(0, std::min(4, static_cast<int>(y * 5.0 / std::max(1, h))));
  return kLineChoices[row];
}

int Toolbox::spray_radius_at_y(double y) const {
  const int h = spray_radii_.get_allocated_height();
  const int row = std::max(0, std::min(4, static_cast<int>(y * 5.0 / std::max(1, h))));
  return kSprayChoices[row];
}

int Toolbox::brush_tip_at(double x, double y) const {
  const int w = std::max(1, brush_tips_.get_allocated_width());
  const int h = std::max(1, brush_tips_.get_allocated_height());
  const int col = std::max(0, std::min(kBrushCols - 1, static_cast<int>(x * kBrushCols / w)));
  const int row = std::max(0, std::min(kBrushRows - 1, static_cast<int>(y * kBrushRows / h)));
  return row * kBrushCols + col;
}

bool Toolbox::on_line_width_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = line_widths_.get_allocated_width();
  const int h = line_widths_.get_allocated_height();
  fill_picker_bg(*this, cr, w, h);

  const int marked = marked_line_width(line_width_, kLineChoices, 5);
  int best = -1;
  for (int j = 0; j < 5; ++j) {
    if (kLineChoices[j] == marked) {
      best = j;
      break;
    }
  }

  const double row_h = h / 5.0;
  for (int i = 0; i < 5; ++i) {
    const double cy = row_h * (i + 0.5);
    const int lw = kLineChoices[i];
    if (best >= 0 && i == best) {
      cr->set_source_rgb(0.1, 0.1, 0.1);
      cr->move_to(4, cy);
      cr->line_to(7, cy + 3);
      cr->line_to(12, cy - 4);
      cr->set_line_width(1.5);
      cr->stroke();
    }
    cr->set_source_rgb(0.05, 0.05, 0.05);
    if (i == 0) {
      for (int x = 14; x < w - 4; x += 3) {
        cr->rectangle(x, cy - 0.5, 1.5, 1.0);
        cr->fill();
      }
    } else {
      cr->set_line_width(static_cast<double>(lw));
      cr->set_line_cap(Cairo::LINE_CAP_BUTT);
      cr->move_to(14, cy);
      cr->line_to(w - 4, cy);
      cr->stroke();
    }
  }
  return true;
}

bool Toolbox::on_line_width_press(GdkEventButton* event) {
  if (event == nullptr || event->button != 1) {
    return false;
  }
  const int w = width_at_y(event->y);
  set_line_width(w);
  if (on_line_width_chosen) {
    on_line_width_chosen(w);
  }
  return true;
}

bool Toolbox::on_brush_tips_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = brush_tips_.get_allocated_width();
  const int h = brush_tips_.get_allocated_height();
  fill_picker_bg(*this, cr, w, h);

  const double cell_w = w / static_cast<double>(kBrushCols);
  const double cell_h = h / static_cast<double>(kBrushRows);
  for (int i = 0; i < kBrushTipCount; ++i) {
    const int col = i % kBrushCols;
    const int row = i / kBrushCols;
    const double cx = cell_w * (col + 0.5);
    const double cy = cell_h * (row + 0.5);
    if (i == brush_tip_) {
      cr->set_source_rgb(0.75, 0.82, 0.95);
      cr->rectangle(cell_w * col + 1, cell_h * row + 1, cell_w - 2, cell_h - 2);
      cr->fill();
    }
    const BrushTip tip = lundukepaint::brush_tip_at(i);
    cr->set_source_rgb(0.05, 0.05, 0.05);
    const int half = std::max(1, tip.size / 2);
    auto paint = [&](int dx, int dy) {
      cr->rectangle(cx + dx - 0.5, cy + dy - 0.5, 1.0, 1.0);
      cr->fill();
    };
    switch (tip.kind) {
      case BrushTipKind::Round: {
        const double r = tip.size * 0.5;
        cr->arc(cx, cy, std::max(0.6, r), 0, 6.283185307179586);
        cr->fill();
        break;
      }
      case BrushTipKind::Square:
        cr->rectangle(cx - half, cy - half, tip.size, tip.size);
        cr->fill();
        break;
      case BrushTipKind::Slash:
        cr->set_line_width(std::max(1.0, tip.size / 4.0));
        cr->move_to(cx - half, cy + half);
        cr->line_to(cx + half, cy - half);
        cr->stroke();
        break;
      case BrushTipKind::Backslash:
        cr->set_line_width(std::max(1.0, tip.size / 4.0));
        cr->move_to(cx - half, cy - half);
        cr->line_to(cx + half, cy + half);
        cr->stroke();
        break;
      case BrushTipKind::HBar:
        cr->set_line_width(std::max(1.0, tip.size / 5.0));
        cr->move_to(cx - half, cy);
        cr->line_to(cx + half, cy);
        cr->stroke();
        break;
      case BrushTipKind::VBar:
        cr->set_line_width(std::max(1.0, tip.size / 5.0));
        cr->move_to(cx, cy - half);
        cr->line_to(cx, cy + half);
        cr->stroke();
        break;
      case BrushTipKind::Cross:
        cr->set_line_width(std::max(1.0, tip.size / 5.0));
        cr->move_to(cx - half, cy);
        cr->line_to(cx + half, cy);
        cr->move_to(cx, cy - half);
        cr->line_to(cx, cy + half);
        cr->stroke();
        break;
    }
    (void)paint;
    cr->set_source_rgb(0.55, 0.55, 0.55);
    cr->rectangle(cell_w * col + 0.5, cell_h * row + 0.5, cell_w - 1.0, cell_h - 1.0);
    cr->set_line_width(1.0);
    cr->stroke();
  }
  return true;
}

bool Toolbox::on_brush_tips_press(GdkEventButton* event) {
  if (event == nullptr || event->button != 1) {
    return false;
  }
  const int index = brush_tip_at(event->x, event->y);
  set_brush_tip(index);
  if (on_brush_tip_chosen) {
    on_brush_tip_chosen(brush_tip_);
  }
  return true;
}

bool Toolbox::on_spray_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = spray_radii_.get_allocated_width();
  const int h = spray_radii_.get_allocated_height();
  fill_picker_bg(*this, cr, w, h);

  int best = 0;
  int bestd = 999;
  for (int j = 0; j < 5; ++j) {
    const int d = std::abs(spray_radius_ - kSprayChoices[j]);
    if (d < bestd) {
      bestd = d;
      best = j;
    }
  }

  const double row_h = h / 5.0;
  for (int i = 0; i < 5; ++i) {
    const double cy = row_h * (i + 0.5);
    const int rad = kSprayChoices[i];
    if (i == best) {
      cr->set_source_rgb(0.1, 0.1, 0.1);
      cr->move_to(4, cy);
      cr->line_to(7, cy + 3);
      cr->line_to(12, cy - 4);
      cr->set_line_width(1.5);
      cr->stroke();
    }
    cr->set_source_rgb(0.05, 0.05, 0.05);
    const double draw_r = std::min(row_h * 0.35, rad * 0.35);
    cr->arc(w * 0.55, cy, std::max(1.0, draw_r), 0, 6.283185307179586);
    cr->set_line_width(1.0);
    cr->stroke();
    // Speckle dots inside.
    cr->set_source_rgb(0.15, 0.15, 0.15);
    for (int k = 0; k < 6 + i * 2; ++k) {
      const double ang = k * 0.9 + i;
      const double rr = draw_r * (0.2 + 0.6 * ((k * 37) % 100) / 100.0);
      cr->rectangle(w * 0.55 + std::cos(ang) * rr - 0.5, cy + std::sin(ang) * rr - 0.5, 1.2, 1.2);
      cr->fill();
    }
  }
  return true;
}

bool Toolbox::on_spray_press(GdkEventButton* event) {
  if (event == nullptr || event->button != 1) {
    return false;
  }
  const int r = spray_radius_at_y(event->y);
  set_spray_radius(r);
  if (on_spray_radius_chosen) {
    on_spray_radius_chosen(r);
  }
  return true;
}

namespace {

void paint_well(const Cairo::RefPtr<Cairo::Context>& cr, int x, int y, int size, Color color) {
  if (color.a == 0) {
    const int cell = std::max(2, size / 4);
    for (int py = 0; py < size; py += cell) {
      for (int px = 0; px < size; px += cell) {
        const bool dark = ((px / cell) + (py / cell)) % 2 == 0;
        cr->set_source_rgb(dark ? 0.75 : 0.92, dark ? 0.75 : 0.92, dark ? 0.75 : 0.92);
        cr->rectangle(x + px, y + py, std::min(cell, size - px), std::min(cell, size - py));
        cr->fill();
      }
    }
  } else {
    cr->set_source_rgba(color.r / 255.0, color.g / 255.0, color.b / 255.0, color.a / 255.0);
    cr->rectangle(x, y, size, size);
    cr->fill();
  }
  cr->set_source_rgb(0.05, 0.05, 0.05);
  cr->rectangle(x + 0.5, y + 0.5, size - 1.0, size - 1.0);
  cr->set_line_width(1.0);
  cr->stroke();
}

}  // namespace

bool Toolbox::on_fg_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = std::max(1, fg_well_.get_allocated_width());
  const int h = std::max(1, fg_well_.get_allocated_height());
  paint_well(cr, 0, 0, std::min(w, h), fg_);
  return true;
}

bool Toolbox::on_fg_press(GdkEventButton* event) {
  if (event == nullptr || (event->button != 1 && event->button != 3)) {
    return false;
  }
  if (on_edit_color) {
    on_edit_color(false);
  }
  return true;
}

bool Toolbox::on_bg_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = std::max(1, bg_well_.get_allocated_width());
  const int h = std::max(1, bg_well_.get_allocated_height());
  paint_well(cr, 0, 0, std::min(w, h), bg_);
  return true;
}

bool Toolbox::on_bg_press(GdkEventButton* event) {
  if (event == nullptr || (event->button != 1 && event->button != 3)) {
    return false;
  }
  if (on_edit_color) {
    on_edit_color(true);
  }
  return true;
}

bool Toolbox::on_swap_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = std::max(1, swap_colors_.get_allocated_width());
  const int h = std::max(1, swap_colors_.get_allocated_height());
  fill_picker_bg(*this, cr, w, h);
  cr->set_source_rgb(0.1, 0.1, 0.1);
  cr->set_line_width(1.4);
  cr->move_to(4, h * 0.35);
  cr->line_to(w - 6, h * 0.35);
  cr->line_to(w - 9, h * 0.35 - 3);
  cr->move_to(w - 6, h * 0.35);
  cr->line_to(w - 9, h * 0.35 + 3);
  cr->move_to(w - 4, h * 0.68);
  cr->line_to(6, h * 0.68);
  cr->line_to(9, h * 0.68 - 3);
  cr->move_to(6, h * 0.68);
  cr->line_to(9, h * 0.68 + 3);
  cr->stroke();
  return true;
}

bool Toolbox::on_swap_press(GdkEventButton* event) {
  if (event == nullptr || event->button != 1) {
    return false;
  }
  if (on_swap_colors) {
    on_swap_colors();
  }
  return true;
}

bool Toolbox::on_reset_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const int w = std::max(1, reset_colors_.get_allocated_width());
  const int h = std::max(1, reset_colors_.get_allocated_height());
  fill_picker_bg(*this, cr, w, h);
  paint_well(cr, 6, 6, 10, Color::white());
  paint_well(cr, 2, 2, 10, Color::black());
  (void)w;
  (void)h;
  return true;
}

bool Toolbox::on_reset_press(GdkEventButton* event) {
  if (event == nullptr || event->button != 1) {
    return false;
  }
  if (on_reset_colors) {
    on_reset_colors();
  }
  return true;
}

}  // namespace lundukepaint
