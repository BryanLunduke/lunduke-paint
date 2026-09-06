// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/canvas_view.hpp"

#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
#include "raster/blend.hpp"
#include "tools/selection_xform.hpp"
#include "ui/intro_howdy.hpp"

#include <glib.h>
#include <glibmm/main.h>
#include <gtkmm/adjustment.h>

#include <algorithm>
#include <cmath>
#include <vector>

namespace lundukepaint {
namespace {

constexpr double kZoomMin = 0.125;
constexpr double kZoomMax = 16.0;

// The scrolled window never asks its ancestors for more than this, whatever the
// image size is: scrollbars take care of the overflow, so opening a 4000-pixel
// photo cannot inflate the toplevel window.
constexpr int kMinContentWidth = 240;
constexpr int kMinContentHeight = 180;

void write_argb32(std::uint8_t* dst, Color c) {
  const int a = c.a;
  dst[0] = static_cast<std::uint8_t>((static_cast<int>(c.b) * a + 127) / 255);
  dst[1] = static_cast<std::uint8_t>((static_cast<int>(c.g) * a + 127) / 255);
  dst[2] = static_cast<std::uint8_t>((static_cast<int>(c.r) * a + 127) / 255);
  dst[3] = c.a;
}


// Preview floating selection without wiping the canvas to the transparency
// checker. Cut-holes hide only the active layer; float pixels are alpha-
// composited (transparent-move keeps empty float pixels see-through;
// opaque-move fills empty float pixels with the BG well color).
Color apply_floating_overlay(const Document* document, Color c, int x, int y) {
  if (document == nullptr) {
    return c;
  }
  const Selection& sel = document->selection();
  if (!sel.floating()) {
    return c;
  }
  if (!sel.copy_mode() && sel.origin_rect().contains(x, y)) {
    // Cut-hole preview: hide the active layer, keep layers below; never fall
    // through to the transparency checker on an otherwise opaque canvas.
    Color below = document->layers().composite_pixel(x, y, nullptr, -1,
                                                     document->layers().active_index());
    if (below.a != 0) {
      c = below;
    } else {
      Color bg = document->canvas_background();
      c = (bg.a != 0) ? bg : Color::white();
    }
  }
  if (sel.float_rect().contains(x, y)) {
    const Color f = sel.float_pixel(x - sel.float_x(), y - sel.float_y());
    if (f.a == 0) {
      if (!sel.transparent_move()) {
        c = document->background();
      }
      // transparent_move: leave underlying c
    } else if (f.a == 255) {
      c = f;
    } else {
      std::uint8_t dest[4] = {c.r, c.g, c.b, c.a};
      const std::uint8_t src[4] = {f.r, f.g, f.b, f.a};
      blend_pixel(dest, src, BlendMode::Normal, 1.0f);
      c = Color{dest[0], dest[1], dest[2], dest[3]};
    }
  }
  return c;
}

void draw_checker(const Cairo::RefPtr<Cairo::Context>& cr, int x, int y, int w, int h, Color light,
                  Color dark) {
  const int cell = 8;
  const int x0 = x;
  const int y0 = y;
  const int x1 = x + w;
  const int y1 = y + h;
  for (int cy = y0; cy < y1; cy += cell) {
    for (int cx = x0; cx < x1; cx += cell) {
      const int tx = ((cx - x0) / cell) + ((cy - y0) / cell);
      const Color c = ((tx & 1) == 0) ? light : dark;
      cr->set_source_rgb(c.r / 255.0, c.g / 255.0, c.b / 255.0);
      const int cw = std::min(cell, x1 - cx);
      const int ch = std::min(cell, y1 - cy);
      cr->rectangle(cx, cy, cw, ch);
      cr->fill();
    }
  }
}

}  // namespace

CanvasView::CanvasView() {
  set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  set_hexpand(true);
  set_vexpand(true);
  set_shadow_type(Gtk::SHADOW_IN);
  // Keep our own size request small and bounded. min-content-* pins the
  // minimum, and propagate-natural-* stays off so the inner Gtk::Layout's
  // content-sized request (see update_area_size()) never reaches the window.
  set_min_content_width(kMinContentWidth);
  set_min_content_height(kMinContentHeight);
  set_propagate_natural_width(false);
  set_propagate_natural_height(false);

  area_.set_can_focus(true);
  area_.set_hexpand(false);
  area_.set_vexpand(false);
  area_.add_events(Gdk::POINTER_MOTION_MASK | Gdk::LEAVE_NOTIFY_MASK | Gdk::BUTTON_PRESS_MASK |
                   Gdk::BUTTON_RELEASE_MASK | Gdk::SCROLL_MASK | Gdk::SMOOTH_SCROLL_MASK |
                   Gdk::BUTTON_MOTION_MASK);

  area_.signal_draw().connect(sigc::mem_fun(*this, &CanvasView::on_area_draw), false);
  area_.signal_motion_notify_event().connect(sigc::mem_fun(*this, &CanvasView::on_area_motion),
                                             false);
  area_.signal_leave_notify_event().connect(sigc::mem_fun(*this, &CanvasView::on_area_leave),
                                            false);
  area_.signal_button_press_event().connect(sigc::mem_fun(*this, &CanvasView::on_area_button_press),
                                            false);
  area_.signal_button_release_event().connect(
      sigc::mem_fun(*this, &CanvasView::on_area_button_release), false);
  area_.signal_scroll_event().connect(sigc::mem_fun(*this, &CanvasView::on_area_scroll), false);

  layout_.set_hexpand(true);
  layout_.set_vexpand(true);
  layout_.put(area_, 0, 0);
  add(layout_);
  update_area_size();
}

void CanvasView::set_document(Document* document) {
  cancel_intro();
  document_ = document;
  update_area_size();
  queue_draw();
  signal_view_changed_.emit();
}

void CanvasView::set_tool(Tool* tool) {
  tool_ = tool;
}

void CanvasView::set_space_down(bool down) {
  space_down_ = down;
}

void CanvasView::reset_blank() {
  update_area_size();
  invalidate_all();
}

void CanvasView::invalidate_rect(Rect rect) {
  if (rect.empty()) {
    invalidate_all();
    return;
  }
  const int ox = origin_x();
  const int oy = origin_y();
  const double z = zoom_;
  const int x = static_cast<int>(std::floor(ox + rect.x * z)) - 2;
  const int y = static_cast<int>(std::floor(oy + rect.y * z)) - 2;
  const int w = static_cast<int>(std::ceil(rect.w * z)) + 4;
  const int h = static_cast<int>(std::ceil(rect.h * z)) + 4;
  area_.queue_draw_area(x, y, w, h);
}

void CanvasView::invalidate_all() {
  area_.queue_draw();
}

void CanvasView::refresh_size() {
  update_area_size();
}

void CanvasView::recenter_in_viewport() {
  update_area_size();
  if (const auto hadj = get_hadjustment()) {
    const double upper = hadj->get_upper();
    const double page = hadj->get_page_size();
    const double target = upper > page ? (upper - page) * 0.5 : 0.0;
    hadj->set_value(std::clamp(target, hadj->get_lower(), std::max(hadj->get_lower(), upper - page)));
  }
  if (const auto vadj = get_vadjustment()) {
    const double upper = vadj->get_upper();
    const double page = vadj->get_page_size();
    const double target = upper > page ? (upper - page) * 0.5 : 0.0;
    vadj->set_value(std::clamp(target, vadj->get_lower(), std::max(vadj->get_lower(), upper - page)));
  }
  invalidate_all();
}

void CanvasView::visible_center(double& x, double& y) const {
  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  x = hadj ? hadj->get_value() + hadj->get_page_size() * 0.5 : area_.get_allocated_width() * 0.5;
  y = vadj ? vadj->get_value() + vadj->get_page_size() * 0.5 : area_.get_allocated_height() * 0.5;
}

void CanvasView::set_zoom(double zoom) {
  double x = 0;
  double y = 0;
  visible_center(x, y);
  zoom_to(zoom, x, y);
}

void CanvasView::zoom_in() {
  double x = 0;
  double y = 0;
  visible_center(x, y);
  zoom_in_at(x, y);
}

void CanvasView::zoom_out() {
  double x = 0;
  double y = 0;
  visible_center(x, y);
  zoom_out_at(x, y);
}

void CanvasView::zoom_in_at(double widget_x, double widget_y) {
  zoom_to(zoom_ * 2.0, widget_x, widget_y);
}

void CanvasView::zoom_out_at(double widget_x, double widget_y) {
  zoom_to(zoom_ * 0.5, widget_x, widget_y);
}

double CanvasView::snapped_zoom(double zoom) const {
  zoom = std::clamp(zoom, kZoomMin, kZoomMax);
  static const double steps[] = {0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0};
  double best = steps[0];
  double best_d = std::abs(zoom - best);
  for (double step : steps) {
    const double d = std::abs(zoom - step);
    if (d < best_d) {
      best = step;
      best_d = d;
    }
  }
  return best;
}

void CanvasView::zoom_to(double zoom, double widget_x, double widget_y) {
  zoom = snapped_zoom(zoom);
  if (std::abs(zoom - zoom_) < 1e-6) {
    return;
  }

  double canvas_x = 0;
  double canvas_y = 0;
  widget_to_canvas(widget_x, widget_y, canvas_x, canvas_y);

  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();

  zoom_ = zoom;
  update_area_size();

  // Keep the canvas point under the pointer after the size change.
  if (hadj) {
    const double new_widget_x = origin_x() + canvas_x * zoom_;
    hadj->set_value(new_widget_x - widget_x);
  }
  if (vadj) {
    const double new_widget_y = origin_y() + canvas_y * zoom_;
    vadj->set_value(new_widget_y - widget_y);
  }

  invalidate_all();
  signal_view_changed_.emit();
}

int CanvasView::canvas_width() const {
  return document_ != nullptr ? document_->width() : kDefaultWidth;
}

int CanvasView::canvas_height() const {
  return document_ != nullptr ? document_->height() : kDefaultHeight;
}

Color CanvasView::sample_pixel(int canvas_x, int canvas_y) const {
  if (document_ == nullptr) {
    return Color::transparent();
  }
  const bool stroking = tool_ != nullptr && tool_->is_stroking();
  const Layer* tool = stroking ? &document_->layers().tool_layer() : nullptr;
  const int tool_i = stroking ? document_->layers().active_index() : -1;
  Color c = document_->layers().composite_pixel(canvas_x, canvas_y, tool, tool_i);
  return apply_floating_overlay(document_, c, canvas_x, canvas_y);
}

void CanvasView::widget_to_canvas(double widget_x, double widget_y, double& canvas_x,
                                 double& canvas_y) const {
  canvas_x = (widget_x - origin_x()) / zoom_;
  canvas_y = (widget_y - origin_y()) / zoom_;
}

bool CanvasView::canvas_to_screen(int canvas_x, int canvas_y, int& screen_x, int& screen_y) const {
  const int wx = static_cast<int>(static_cast<double>(origin_x()) +
                                  static_cast<double>(canvas_x) * zoom_);
  const int wy = static_cast<int>(static_cast<double>(origin_y()) +
                                  static_cast<double>(canvas_y) * zoom_);
  Gtk::DrawingArea& area = const_cast<Gtk::DrawingArea&>(area_);
  // Go through the toplevel rather than the drawing area's own GdkWindow: the
  // area is moved inside the Gtk::Layout to centre the canvas and is shifted
  // again by the scroll offset, and translate_coordinates() accounts for both
  // whether or not the area happens to own a window.
  Gtk::Widget* top = area.get_toplevel();
  if (top != nullptr && top->get_is_toplevel()) {
    int tx = 0;
    int ty = 0;
    if (area.translate_coordinates(*top, wx, wy, tx, ty)) {
      if (const auto win = top->get_window()) {
        int ox = 0;
        int oy = 0;
        win->get_origin(ox, oy);
        screen_x = ox + tx;
        screen_y = oy + ty;
        return true;
      }
    }
  }
  const auto win = area.get_window();
  if (!win) {
    return false;
  }
  int ox = 0;
  int oy = 0;
  win->get_origin(ox, oy);
  screen_x = ox + wx;
  screen_y = oy + wy;
  return true;
}

int CanvasView::content_pixel_width() const {
  return static_cast<int>(std::ceil(canvas_width() * zoom_));
}

int CanvasView::content_pixel_height() const {
  return static_cast<int>(std::ceil(canvas_height() * zoom_));
}

int CanvasView::origin_x() const {
  return margin();
}

int CanvasView::origin_y() const {
  return margin();
}

void CanvasView::focus_canvas() {
  if (area_.get_realized() || area_.get_mapped()) {
    area_.grab_focus();
  }
}

void CanvasView::start_intro() {
  if (intro_active_) {
    return;
  }
  intro_timer_.disconnect();
  intro_held_ = false;
  intro_active_ = true;
  intro_start_us_ = g_get_monotonic_time();
  intro_timer_ = Glib::signal_timeout().connect(sigc::mem_fun(*this, &CanvasView::on_intro_tick),
                                                16);
  invalidate_all();
}

void CanvasView::cancel_intro() {
  if (!intro_active_ && !intro_held_) {
    return;
  }
  intro_active_ = false;
  intro_held_ = false;
  intro_timer_.disconnect();
  invalidate_all();
}

void CanvasView::skip_intro() {
  // Skip aborts a running animation. A completed howdy stays on the canvas.
  if (!intro_active_) {
    return;
  }
  cancel_intro();
}

bool CanvasView::on_intro_tick() {
  if (!intro_active_) {
    return false;
  }
  if (intro::finished(g_get_monotonic_time() - intro_start_us_)) {
    intro_active_ = false;
    intro_held_ = true;
    invalidate_all();  // last frame is the finished word; leave it there
    return false;
  }
  invalidate_all();
  return true;
}

void CanvasView::draw_intro(const Cairo::RefPtr<Cairo::Context>& cr) {
  const double p =
      intro_held_ ? 1.0 : intro::progress(g_get_monotonic_time() - intro_start_us_);
  if (p <= 0.0) {
    return;
  }
  const int dw = content_pixel_width();
  const int dh = content_pixel_height();
  if (dw < 48 || dh < 32) {
    return;
  }
  int size_px = std::clamp(static_cast<int>(dh * 0.45), 18, 190);
  intro::Ink ink;
  if (!intro::measure(size_px, ink) || ink.width <= 0) {
    return;
  }
  const double max_width = dw * 0.7;
  if (ink.width > max_width) {
    size_px = std::max(14, static_cast<int>(size_px * max_width / ink.width));
    if (!intro::measure(size_px, ink) || ink.width <= 0) {
      return;
    }
  }
  const double x = origin_x() + (dw - ink.width) * 0.5;
  const double y = origin_y() + (dh - ink.height) * 0.5;
  intro::draw(cr->cobj(), x, y, size_px, p);
}

void CanvasView::on_size_allocate(Gtk::Allocation& allocation) {
  Gtk::ScrolledWindow::on_size_allocate(allocation);
  update_area_size();
}

void CanvasView::update_area_size() {
  if (updating_size_) {
    return;
  }
  const int aw = content_pixel_width() + margin() * 2;
  const int ah = content_pixel_height() + margin() * 2;
  int vw = 0;
  int vh = 0;
  if (const auto hadj = get_hadjustment()) {
    vw = static_cast<int>(hadj->get_page_size());
  }
  if (const auto vadj = get_vadjustment()) {
    vh = static_cast<int>(vadj->get_page_size());
  }
  if (vw < 2) {
    vw = get_allocated_width();
  }
  if (vh < 2) {
    vh = get_allocated_height();
  }
  const int lw = std::max(aw, vw);
  const int lh = std::max(ah, vh);
  const int x = std::max(0, (lw - aw) / 2);
  const int y = std::max(0, (lh - ah) / 2);
  updating_size_ = true;
  area_.set_size_request(aw, ah);
  layout_.set_size(static_cast<guint>(lw), static_cast<guint>(lh));
  layout_.move(area_, x, y);
  updating_size_ = false;
}

unsigned CanvasView::modifiers_from_state(guint state) const {
  unsigned mods = 0;
  if (state & GDK_SHIFT_MASK) {
    mods |= Modifier::Shift;
  }
  if (state & GDK_CONTROL_MASK) {
    mods |= Modifier::Ctrl;
  }
  if (state & GDK_MOD1_MASK) {
    mods |= Modifier::Alt;
  }
  return mods;
}

CanvasEvent CanvasView::make_event(double widget_x, double widget_y, unsigned button,
                                   guint state) const {
  CanvasEvent event;
  widget_to_canvas(widget_x, widget_y, event.x, event.y);
  event.button = button;
  event.modifiers = modifiers_from_state(state);
  return event;
}

void CanvasView::begin_pan(double widget_x, double widget_y) {
  panning_ = true;
  pan_start_x_ = widget_x;
  pan_start_y_ = widget_y;
  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  pan_hadj_ = hadj ? hadj->get_value() : 0.0;
  pan_vadj_ = vadj ? vadj->get_value() : 0.0;
}

void CanvasView::update_pan(double widget_x, double widget_y) {
  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  if (hadj) {
    hadj->set_value(pan_hadj_ - (widget_x - pan_start_x_));
  }
  if (vadj) {
    vadj->set_value(pan_vadj_ - (widget_y - pan_start_y_));
  }
}

bool CanvasView::on_area_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
  const Gtk::Allocation alloc = area_.get_allocation();
  auto context = area_.get_style_context();
  Gdk::RGBA bg;
  if (!context->lookup_color("theme_bg_color", bg)) {
    bg.set_rgba(0.85, 0.85, 0.85, 1.0);
  }
  cr->set_source_rgb(bg.get_red(), bg.get_green(), bg.get_blue());
  cr->paint();

  const int ox = origin_x();
  const int oy = origin_y();
  const int cw = canvas_width();
  const int ch = canvas_height();
  const int dw = content_pixel_width();
  const int dh = content_pixel_height();

  cr->save();
  cr->rectangle(ox, oy, dw, dh);
  cr->clip();
  draw_checker(cr, ox, oy, dw, dh, checker_light_, checker_dark_);

  if (document_ == nullptr) {
    cr->restore();
    return true;
  }

  double clip_x1 = 0;
  double clip_y1 = 0;
  double clip_x2 = 0;
  double clip_y2 = 0;
  cr->get_clip_extents(clip_x1, clip_y1, clip_x2, clip_y2);

  const int vis_x0 = std::max(0, static_cast<int>(std::floor((clip_x1 - ox) / zoom_)));
  const int vis_y0 = std::max(0, static_cast<int>(std::floor((clip_y1 - oy) / zoom_)));
  const int vis_x1 = std::min(cw, static_cast<int>(std::ceil((clip_x2 - ox) / zoom_)));
  const int vis_y1 = std::min(ch, static_cast<int>(std::ceil((clip_y2 - oy) / zoom_)));
  if (vis_x1 <= vis_x0 || vis_y1 <= vis_y0) {
    cr->restore();
    return true;
  }

  const bool stroking = tool_ != nullptr && tool_->is_stroking();
  const Layer* tool_override = stroking ? &document_->layers().tool_layer() : nullptr;
  const int tool_index = stroking ? document_->layers().active_index() : -1;
  const int sw = vis_x1 - vis_x0;
  const int sh = vis_y1 - vis_y0;
  std::vector<std::uint8_t> flat(static_cast<std::size_t>(sw) * static_cast<std::size_t>(sh) * 4, 0);
  document_->layers().composite_rect(flat.data(), sw * 4, Rect{vis_x0, vis_y0, sw, sh},
                                     tool_override, tool_index);

  auto sample_flat = [&](int sx, int sy) {
    Color c;
    if (sx < vis_x0 || sy < vis_y0 || sx >= vis_x1 || sy >= vis_y1) {
      return Color::transparent();
    }
    const std::uint8_t* p =
        flat.data() + static_cast<std::size_t>((sy - vis_y0) * sw + (sx - vis_x0)) * 4;
    c = Color{p[0], p[1], p[2], p[3]};
    return display_pixel(c, sx, sy);
  };

  const bool integer_up = zoom_ >= 1.0 - 1e-9;
  if (integer_up) {
    const int dest_x = static_cast<int>(std::floor(ox + vis_x0 * zoom_));
    const int dest_y = static_cast<int>(std::floor(oy + vis_y0 * zoom_));
    const int dest_w = static_cast<int>(std::ceil((vis_x1 - vis_x0) * zoom_));
    const int dest_h = static_cast<int>(std::ceil((vis_y1 - vis_y0) * zoom_));
    if (dest_w > 0 && dest_h > 0) {
      auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, dest_w, dest_h);
      std::uint8_t* dst = surface->get_data();
      const int dst_stride = surface->get_stride();
      const int iz = std::max(1, static_cast<int>(std::lround(zoom_)));
      const bool exact = std::abs(zoom_ - static_cast<double>(iz)) < 1e-6;
      for (int dy = 0; dy < dest_h; ++dy) {
        int sy = vis_y0;
        if (exact) {
          sy = vis_y0 + dy / iz;
        } else {
          sy = vis_y0 + static_cast<int>(std::floor(dy / zoom_));
        }
        if (sy >= ch) {
          sy = ch - 1;
        }
        std::uint8_t* drow = dst + static_cast<std::size_t>(dy) * dst_stride;
        for (int dx = 0; dx < dest_w; ++dx) {
          int sx = vis_x0;
          if (exact) {
            sx = vis_x0 + dx / iz;
          } else {
            sx = vis_x0 + static_cast<int>(std::floor(dx / zoom_));
          }
          if (sx >= cw) {
            sx = cw - 1;
          }
          write_argb32(drow + static_cast<std::size_t>(dx) * 4, sample_flat(sx, sy));
        }
      }
      surface->mark_dirty();
      cr->set_source(surface, dest_x, dest_y);
      cr->paint();
    }
  } else {
    const int sw = vis_x1 - vis_x0;
    const int sh = vis_y1 - vis_y0;
    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, sw, sh);
    std::uint8_t* dst = surface->get_data();
    const int dst_stride = surface->get_stride();
    for (int y = 0; y < sh; ++y) {
      std::uint8_t* drow = dst + static_cast<std::size_t>(y) * dst_stride;
      for (int x = 0; x < sw; ++x) {
        write_argb32(drow + static_cast<std::size_t>(x) * 4,
                     sample_flat(vis_x0 + x, vis_y0 + y));
      }
    }
    surface->mark_dirty();
    cr->save();
    cr->translate(ox + vis_x0 * zoom_, oy + vis_y0 * zoom_);
    cr->scale(zoom_, zoom_);
    cr->set_source(surface, 0, 0);
    cr->paint();
    cr->restore();
  }

  if (grid_visible_ && zoom_ + 1e-9 >= static_cast<double>(grid_threshold_) / 100.0) {
    draw_pixel_grid(cr, ox, oy, vis_x0, vis_y0, vis_x1, vis_y1);
  }
  draw_marching_ants(cr, ox, oy);
  if (document_ != nullptr) {
    draw_selection_handles(cr, document_->selection(), ox, oy, zoom_);
  }
  if (intro_active_ || intro_held_) {
    draw_intro(cr);
  }
  ensure_ants_timer();

  cr->restore();
  (void)alloc;
  return true;
}

bool CanvasView::on_area_motion(GdkEventMotion* event) {
  if (event == nullptr) {
    return false;
  }
  double cx = 0;
  double cy = 0;
  widget_to_canvas(event->x, event->y, cx, cy);
  has_pointer_ = true;
  last_cx_ = cx;
  last_cy_ = cy;
  signal_pointer_moved_.emit(cx, cy);

  if (panning_) {
    update_pan(event->x, event->y);
    return true;
  }
  if (tool_ != nullptr && tool_->is_stroking()) {
    tool_->on_motion(make_event(event->x, event->y, 0, event->state));
    return true;
  }
  return false;
}

bool CanvasView::on_area_leave(GdkEventCrossing* /*event*/) {
  has_pointer_ = false;
  signal_pointer_left_.emit();
  return false;
}

bool CanvasView::on_area_button_press(GdkEventButton* event) {
  if (event == nullptr) {
    return false;
  }
  cancel_intro();
  area_.grab_focus();
  if (event->button == 2 || (event->button == 1 && space_down_)) {
    begin_pan(event->x, event->y);
    return true;
  }
  if (tool_ != nullptr && (event->button == 1 || event->button == 3)) {
    if (event->type == GDK_2BUTTON_PRESS) {
      tool_->on_double_click(make_event(event->x, event->y, event->button, event->state));
      return true;
    }
    if (event->type == GDK_BUTTON_PRESS) {
      tool_->on_press(make_event(event->x, event->y, event->button, event->state));
      return true;
    }
  }
  return false;
}

bool CanvasView::on_area_button_release(GdkEventButton* event) {
  if (event == nullptr) {
    return false;
  }
  if (panning_ && (event->button == 2 || event->button == 1)) {
    panning_ = false;
    return true;
  }
  if (tool_ != nullptr && (event->button == 1 || event->button == 3)) {
    tool_->on_release(make_event(event->x, event->y, event->button, event->state));
    return true;
  }
  return false;
}

bool CanvasView::on_area_scroll(GdkEventScroll* event) {
  if (event == nullptr) {
    return false;
  }
  if ((event->state & GDK_CONTROL_MASK) == 0) {
    return false;
  }
  bool zoom_in = false;
  if (event->direction == GDK_SCROLL_UP) {
    zoom_in = true;
  } else if (event->direction == GDK_SCROLL_DOWN) {
    zoom_in = false;
  } else if (event->direction == GDK_SCROLL_SMOOTH) {
    if (event->delta_y < 0) {
      zoom_in = true;
    } else if (event->delta_y > 0) {
      zoom_in = false;
    } else {
      return true;
    }
  } else {
    return false;
  }
  if (zoom_in) {
    zoom_in_at(event->x, event->y);
  } else {
    zoom_out_at(event->x, event->y);
  }
  return true;
}


Color CanvasView::display_pixel(Color c, int x, int y) const {
  return apply_floating_overlay(document_, c, x, y);
}

void CanvasView::set_grid_visible(bool visible) {
  if (grid_visible_ == visible) {
    return;
  }
  grid_visible_ = visible;
  invalidate_all();
}

void CanvasView::set_checker_colors(Color light, Color dark) {
  checker_light_ = light;
  checker_dark_ = dark;
  invalidate_all();
}

void CanvasView::set_grid_threshold(int percent) {
  if (percent < 100) {
    percent = 100;
  }
  grid_threshold_ = percent;
  invalidate_all();
}

void CanvasView::zoom_fit() {
  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  double vw = hadj ? hadj->get_page_size() : static_cast<double>(get_allocated_width());
  double vh = vadj ? vadj->get_page_size() : static_cast<double>(get_allocated_height());
  if (vw < 32.0) {
    vw = 32.0;
  }
  if (vh < 32.0) {
    vh = 32.0;
  }
  vw -= static_cast<double>(margin()) * 2.0;
  vh -= static_cast<double>(margin()) * 2.0;
  if (vw < 1.0) {
    vw = 1.0;
  }
  if (vh < 1.0) {
    vh = 1.0;
  }
  const int cw = canvas_width();
  const int ch = canvas_height();
  if (cw < 1 || ch < 1) {
    return;
  }
  set_zoom(std::min(vw / cw, vh / ch));
}

void CanvasView::draw_pixel_grid(const Cairo::RefPtr<Cairo::Context>& cr, int ox, int oy, int vis_x0,
                                int vis_y0, int vis_x1, int vis_y1) {
  cr->save();
  cr->set_line_width(1.0);
  cr->set_source_rgba(0.0, 0.0, 0.0, 0.28);
  for (int x = vis_x0; x <= vis_x1; ++x) {
    const double wx = ox + x * zoom_ + 0.5;
    cr->move_to(wx, oy + vis_y0 * zoom_);
    cr->line_to(wx, oy + vis_y1 * zoom_);
  }
  for (int y = vis_y0; y <= vis_y1; ++y) {
    const double wy = oy + y * zoom_ + 0.5;
    cr->move_to(ox + vis_x0 * zoom_, wy);
    cr->line_to(ox + vis_x1 * zoom_, wy);
  }
  cr->stroke();
  cr->restore();
}

void CanvasView::draw_marching_ants(const Cairo::RefPtr<Cairo::Context>& cr, int ox, int oy) {
  if (document_ == nullptr) {
    return;
  }
  const Selection& sel = document_->selection();
  if (sel.empty()) {
    return;
  }
  auto stroke_rect = [&](Rect r) {
    if (r.empty()) {
      return;
    }
    const double x = ox + r.x * zoom_ + 0.5;
    const double y = oy + r.y * zoom_ + 0.5;
    const double w = r.w * zoom_;
    const double h = r.h * zoom_;
    cr->save();
    std::vector<double> dash{4.0, 4.0};
    cr->set_line_width(1.0);
    cr->set_dash(dash, static_cast<double>(ants_phase_));
    cr->set_source_rgb(0.0, 0.0, 0.0);
    cr->rectangle(x, y, w, h);
    cr->stroke();
    cr->set_dash(dash, static_cast<double>(ants_phase_) + 4.0);
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->rectangle(x, y, w, h);
    cr->stroke();
    cr->restore();
  };
  auto stroke_mask = [&]() {
    if (!sel.has_mask() || sel.mask() == nullptr) {
      stroke_rect(sel.bounds());
      return;
    }
    const Rect b = sel.bounds();
    const int mw = sel.mask_w();
    const int mh = sel.mask_h();
    const std::uint8_t* mask = sel.mask();
    auto inside = [&](int x, int y) -> bool {
      if (x < 0 || y < 0 || x >= mw || y >= mh) {
        return false;
      }
      return mask[static_cast<std::size_t>(y) * static_cast<std::size_t>(mw) +
                  static_cast<std::size_t>(x)] != 0;
    };
    cr->save();
    std::vector<double> dash{4.0, 4.0};
    cr->set_line_width(1.0);
    cr->set_dash(dash, static_cast<double>(ants_phase_));
    cr->set_source_rgb(0.0, 0.0, 0.0);
    for (int y = 0; y < mh; ++y) {
      for (int x = 0; x < mw; ++x) {
        if (!inside(x, y)) {
          continue;
        }
        const double px = ox + (b.x + x) * zoom_;
        const double py = oy + (b.y + y) * zoom_;
        if (!inside(x, y - 1)) {
          cr->move_to(px, py + 0.5);
          cr->line_to(px + zoom_, py + 0.5);
        }
        if (!inside(x, y + 1)) {
          cr->move_to(px, py + zoom_ + 0.5);
          cr->line_to(px + zoom_, py + zoom_ + 0.5);
        }
        if (!inside(x - 1, y)) {
          cr->move_to(px + 0.5, py);
          cr->line_to(px + 0.5, py + zoom_);
        }
        if (!inside(x + 1, y)) {
          cr->move_to(px + zoom_ + 0.5, py);
          cr->line_to(px + zoom_ + 0.5, py + zoom_);
        }
      }
    }
    cr->stroke();
    cr->set_dash(dash, static_cast<double>(ants_phase_) + 4.0);
    cr->set_source_rgb(1.0, 1.0, 1.0);
    for (int y = 0; y < mh; ++y) {
      for (int x = 0; x < mw; ++x) {
        if (!inside(x, y)) {
          continue;
        }
        const double px = ox + (b.x + x) * zoom_;
        const double py = oy + (b.y + y) * zoom_;
        if (!inside(x, y - 1)) {
          cr->move_to(px, py + 0.5);
          cr->line_to(px + zoom_, py + 0.5);
        }
        if (!inside(x, y + 1)) {
          cr->move_to(px, py + zoom_ + 0.5);
          cr->line_to(px + zoom_, py + zoom_ + 0.5);
        }
        if (!inside(x - 1, y)) {
          cr->move_to(px + 0.5, py);
          cr->line_to(px + 0.5, py + zoom_);
        }
        if (!inside(x + 1, y)) {
          cr->move_to(px + zoom_ + 0.5, py);
          cr->line_to(px + zoom_ + 0.5, py + zoom_);
        }
      }
    }
    cr->stroke();
    cr->restore();
  };

  if (sel.inverted()) {
    stroke_rect({0, 0, document_->width(), document_->height()});
    if (sel.has_mask() && !sel.floating()) {
      stroke_mask();
    } else {
      stroke_rect(sel.bounds());
    }
  } else if (sel.has_mask() && !sel.floating()) {
    stroke_mask();
  } else {
    stroke_rect(sel.bounds());
  }
}

void CanvasView::ensure_ants_timer() {
  if (ants_timer_.connected()) {
    return;
  }
  ants_timer_ = Glib::signal_timeout().connect(
      [this]() {
        if (document_ == nullptr || document_->selection().empty()) {
          return true;
        }
        ants_phase_ = (ants_phase_ + 1) % 8;
        invalidate_ants();
        return true;
      },
      90);
}

void CanvasView::invalidate_ants() {
  if (document_ == nullptr || document_->selection().empty()) {
    return;
  }
  const Selection& sel = document_->selection();
  if (sel.inverted()) {
    invalidate_all();
    return;
  }
  invalidate_rect(sel.bounds());
}

bool CanvasView::last_pointer(int& canvas_x, int& canvas_y) const {
  if (!has_pointer_) {
    return false;
  }
  canvas_x = static_cast<int>(std::floor(last_cx_));
  canvas_y = static_cast<int>(std::floor(last_cy_));
  return true;
}

void CanvasView::viewport_center_canvas(int& canvas_x, int& canvas_y) const {
  double wx = 0;
  double wy = 0;
  visible_center(wx, wy);
  double cx = 0;
  double cy = 0;
  widget_to_canvas(wx, wy, cx, cy);
  canvas_x = static_cast<int>(std::floor(cx));
  canvas_y = static_cast<int>(std::floor(cy));
}

void CanvasView::apply_zoom(double zoom) {
  zoom = snapped_zoom(zoom);
  if (std::abs(zoom - zoom_) < 1e-6) {
    return;
  }
  zoom_ = zoom;
  update_area_size();
  invalidate_all();
  signal_view_changed_.emit();
}

}  // namespace lundukepaint
