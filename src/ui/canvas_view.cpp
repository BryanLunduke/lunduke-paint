// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/canvas_view.hpp"

#include <string>

#include "doc/document.hpp"
#include "doc/layer.hpp"
#include "doc/layer_stack.hpp"
#include "doc/selection.hpp"
#include "raster/blend.hpp"
#include "tools/selection_xform.hpp"
#include "ui/intro_howdy.hpp"
#include "ui/zoom_policy.hpp"

#include <gdkmm/cursor.h>
#include <glib.h>
#include <glibmm/main.h>
#include <gtkmm/adjustment.h>

#include <cstring>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace lundukepaint {
namespace {

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

void premultiply_rows(std::uint8_t* pixels, int width, int height, int stride) {
  for (int y = 0; y < height; ++y) {
    std::uint8_t* row = pixels + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      const Color straight{p[0], p[1], p[2], p[3]};
      write_argb32(p, straight);
    }
  }
}

void mix_u64(std::uint64_t& h, std::uint64_t v) {
  h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
}

void mix_ora_node(std::uint64_t& h, const OraNode& node) {
  mix_u64(h, node.is_stack ? 1 : 0);
  mix_u64(h, node.visible ? 1 : 0);
  mix_u64(h, node.isolate ? 1 : 0);
  mix_u64(h, static_cast<std::uint64_t>(node.opacity * 100000.0f));
  mix_u64(h, static_cast<std::uint64_t>(node.blend));
  mix_u64(h, static_cast<std::uint32_t>(node.x));
  mix_u64(h, static_cast<std::uint32_t>(node.y));
  mix_u64(h, static_cast<std::uint64_t>(node.layer_index));
  for (const OraNode& child : node.children) {
    mix_ora_node(h, child);
  }
}

std::uint64_t hole_fingerprint(const Document& document, int skip) {
  const LayerStack& layers = document.layers();
  std::uint64_t h = 0;
  mix_u64(h, static_cast<std::uint64_t>(layers.count()));
  mix_u64(h, static_cast<std::uint64_t>(layers.width()));
  mix_u64(h, static_cast<std::uint64_t>(layers.height()));
  mix_u64(h, static_cast<std::uint64_t>(skip));
  for (int i = 0; i < layers.count(); ++i) {
    if (i == skip) {
      continue;
    }
    const Layer& layer = layers.at(i);
    mix_u64(h, layer.identity());
    mix_u64(h, layer.revision());
    mix_u64(h, layer.visible() ? 1 : 0);
    mix_u64(h, static_cast<std::uint64_t>(layer.opacity() * 100000.0f));
    mix_u64(h, static_cast<std::uint64_t>(layer.blend()));
    mix_u64(h, static_cast<std::uint32_t>(layer.offset_x()));
    mix_u64(h, static_cast<std::uint32_t>(layer.offset_y()));
  }
  if (const OraNode* stack = document.ora_stack()) {
    mix_u64(h, 1);
    mix_ora_node(h, *stack);
  }
  return h;
}


// Preview floating selection without wiping the canvas to the transparency
// checker. Cut-holes hide only the active layer; float pixels are alpha-
// composited (transparent-move keeps empty float pixels see-through;
// opaque-move keeps underlying / canvas BG so empty float pixels stay opaque).
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
    int skip = sel.source_layer();
    if (skip < 0 || skip >= document->layers().count()) {
      skip = document->layers().active_index();
    }
    Color below = document->layers().composite_pixel(x, y, nullptr, -1, skip);
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
        // Opaque-move stamp: keep underlying (post cut-hole) when opaque;
        // otherwise canvas BG / BG well / white — never the checker (R-F03).
        if (c.a == 0) {
          Color bg = document->canvas_background();
          if (bg.a != 0) {
            c = bg;
          } else {
            Color well = document->background();
            c = (well.a != 0) ? well : Color::white();
          }
        }
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

void fill_checker_tile(std::uint8_t* data, int stride, Color light, Color dark) {
  for (int y = 0; y < 16; ++y) {
    std::uint8_t* row = data + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    const Color c = ((y / 8) & 1) == 0 ? light : dark;
    const Color alt = ((y / 8) & 1) == 0 ? dark : light;
    for (int x = 0; x < 16; ++x) {
      const Color src = x < 8 ? c : alt;
      // Cairo RGB24 is actually 32-bit xRGB, little-endian.
      row[static_cast<std::size_t>(x) * 4 + 0] = src.b;
      row[static_cast<std::size_t>(x) * 4 + 1] = src.g;
      row[static_cast<std::size_t>(x) * 4 + 2] = src.r;
      row[static_cast<std::size_t>(x) * 4 + 3] = 255;
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
  signal_realize().connect(sigc::mem_fun(*this, &CanvasView::update_cursor));
  area_.signal_realize().connect(sigc::mem_fun(*this, &CanvasView::update_cursor));
  update_area_size();
}

void CanvasView::update_cursor() {
  const char* id = tool_ != nullptr && tool_->id() != nullptr ? tool_->id() : "";
  if (space_down_ || std::strcmp(id, "hand") == 0) {
    cursor_name_ = "hand";
  } else if (std::strcmp(id, "text") == 0) {
    cursor_name_ = "text";
  } else if (tool_ != nullptr) {
    cursor_name_ = "crosshair";
  } else {
    cursor_name_ = "default";
  }
  auto display = get_display();
  if (!display) {
    return;
  }
  Gdk::CursorType type = Gdk::LEFT_PTR;
  if (std::strcmp(cursor_name_, "hand") == 0) {
    type = Gdk::HAND2;
  } else if (std::strcmp(cursor_name_, "text") == 0) {
    type = Gdk::XTERM;
  } else if (std::strcmp(cursor_name_, "crosshair") == 0) {
    type = Gdk::CROSSHAIR;
  }
  auto cursor = Gdk::Cursor::create(display, type);
  if (auto window = get_window()) {
    window->set_cursor(cursor);
  }
  if (auto window = area_.get_window()) {
    window->set_cursor(cursor);
  }
}

void CanvasView::set_document(Document* document) {
  cancel_intro();
  document_ = document;
  hole_cache_valid_ = false;
  hole_cache_.clear();
  drop_composite_cache();
  ants_path_.reset();
  ants_halos_.clear();
  ants_generation_ = 0;
  update_area_size();
  queue_draw();
  signal_view_changed_.emit();
}

void CanvasView::set_tool(Tool* tool) {
  tool_ = tool;
  update_cursor();
}

void CanvasView::set_space_down(bool down) {
  space_down_ = down;
  update_cursor();
}

void CanvasView::reset_blank() {
  update_area_size();
  invalidate_all();
}

void CanvasView::queue_canvas_area(Rect rect) {
  const int ox = origin_x();
  const int oy = origin_y();
  const double z = zoom_;
  const int x = static_cast<int>(std::floor(ox + rect.x * z)) - 2;
  const int y = static_cast<int>(std::floor(oy + rect.y * z)) - 2;
  const int w = static_cast<int>(std::ceil(rect.w * z)) + 4;
  const int h = static_cast<int>(std::ceil(rect.h * z)) + 4;
  area_.queue_draw_area(x, y, w, h);
}

void CanvasView::drop_composite_cache() {
  composite_cache_.clear();
  composite_valid_ = false;
  composite_dirty_all_ = true;
  composite_dirty_ = {};
  composite_out_w_ = 0;
  composite_out_h_ = 0;
}

void CanvasView::note_composite_dirty(Rect rect) {
  if (!composite_valid_ || composite_dirty_all_) {
    composite_dirty_all_ = true;
    composite_valid_ = false;
    return;
  }
  composite_dirty_ = rect_union(composite_dirty_, rect);
}

void CanvasView::invalidate_rect(Rect rect) {
  if (rect.empty()) {
    invalidate_all();
    return;
  }
  note_composite_dirty(rect);
  queue_canvas_area(rect);
}

void CanvasView::invalidate_all() {
  drop_composite_cache();
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

double CanvasView::snapped_zoom(double zoom) const { return snapped_zoom_step(zoom); }

void CanvasView::zoom_to(double zoom, double widget_x, double widget_y, bool snap) {
  zoom = snap ? snapped_zoom(zoom) : clamp_zoom(zoom);
  if (std::abs(zoom - zoom_) < 1e-6) {
    return;
  }

  double canvas_x = 0;
  double canvas_y = 0;
  widget_to_canvas(widget_x, widget_y, canvas_x, canvas_y);

  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  const double old_scroll_x = hadj ? hadj->get_value() : 0.0;
  const double old_scroll_y = vadj ? vadj->get_value() : 0.0;
  const int old_move_x = area_move_x_;
  const int old_move_y = area_move_y_;

  zoom_ = zoom;
  update_area_size();

  // Event positions are drawing-area coordinates (they already include scroll).
  // layout_.move recenters the area when the canvas is smaller than the viewport,
  // so the scroll also shifts by the change in that centering offset.
  if (hadj) {
    const double new_widget_x = origin_x() + canvas_x * zoom_;
    hadj->set_value(new_widget_x - widget_x + old_scroll_x +
                    static_cast<double>(area_move_x_ - old_move_x));
  }
  if (vadj) {
    const double new_widget_y = origin_y() + canvas_y * zoom_;
    vadj->set_value(new_widget_y - widget_y + old_scroll_y +
                    static_cast<double>(area_move_y_ - old_move_y));
  }

  ants_path_.reset();
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
  const bool tool_preview =
      tool_ != nullptr && tool_->is_stroking() && tool_->uses_tool_layer();
  const Layer* tool = tool_preview ? &document_->layers().tool_layer() : nullptr;
  int tool_i = -1;
  if (tool_preview) {
    tool_i = tool_->preview_layer();
    if (tool_i < 0) {
      tool_i = document_->layers().active_index();
    }
  }
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
  area_move_x_ = x;
  area_move_y_ = y;
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

void CanvasView::begin_pan(double root_x, double root_y) {
  panning_ = true;
  pan_start_x_ = root_x;
  pan_start_y_ = root_y;
  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  pan_hadj_ = hadj ? hadj->get_value() : 0.0;
  pan_vadj_ = vadj ? vadj->get_value() : 0.0;
}

void CanvasView::update_pan(double root_x, double root_y) {
  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  if (hadj) {
    hadj->set_value(pan_hadj_ - (root_x - pan_start_x_));
  }
  if (vadj) {
    vadj->set_value(pan_vadj_ - (root_y - pan_start_y_));
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
  if (!checker_pattern_) {
    rebuild_checker();
  }
  if (checker_pattern_) {
    cr->save();
    cr->translate(ox, oy);
    cr->set_source(checker_pattern_);
    cr->rectangle(0, 0, dw, dh);
    cr->fill();
    cr->restore();
  }

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

  const bool tool_preview =
      tool_ != nullptr && tool_->is_stroking() && tool_->uses_tool_layer();
  const Layer* tool_override = tool_preview ? &document_->layers().tool_layer() : nullptr;
  int tool_index = -1;
  if (tool_preview) {
    tool_index = tool_->preview_layer();
    if (tool_index < 0) {
      tool_index = document_->layers().active_index();
    }
  }
  const Rect view = viewport_canvas_rect(vis_x0, vis_y0, vis_x1, vis_y1);
  if (view.empty()) {
    cr->restore();
    return true;
  }
  const bool downscale = zoom_ < 1.0 - 1e-6;
  const int out_w = downscale ? std::max(1, static_cast<int>(std::ceil(view.w * zoom_))) : view.w;
  const int out_h = downscale ? std::max(1, static_cast<int>(std::ceil(view.h * zoom_))) : view.h;
  ensure_composite(view, out_w, out_h, downscale, tool_override, tool_index);

  if (!blit_surface_ || blit_w_ != out_w || blit_h_ != out_h) {
    blit_surface_ = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, out_w, out_h);
    blit_w_ = out_w;
    blit_h_ = out_h;
  }
  blit_surface_->flush();
  std::uint8_t* dst = blit_surface_->get_data();
  const int dst_stride = blit_surface_->get_stride();
  const int cache_stride = out_w * 4;
  for (int y = 0; y < out_h; ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_stride),
                composite_cache_.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(cache_stride),
                static_cast<std::size_t>(cache_stride));
  }
  if (document_->selection().floating()) {
    const Color canvas_bg = document_->canvas_background();
    const Color well = document_->background();
    const Color hole_clear = canvas_bg.a != 0 ? canvas_bg : Color::white();
    const Color float_clear =
        canvas_bg.a != 0 ? canvas_bg : (well.a != 0 ? well : Color::white());
    if (downscale) {
      paint_scaled_float(dst, dst_stride, view, out_w, out_h);
    } else {
      const Selection& sel = document_->selection();
      const std::uint8_t* hole_rgba = nullptr;
      int hole_stride = 0;
      if (!sel.copy_mode()) {
        int skip = sel.source_layer();
        if (skip < 0 || skip >= document_->layers().count()) {
          skip = document_->layers().active_index();
        }
        const Rect origin = sel.origin_rect();
        const std::uint64_t fp = hole_fingerprint(*document_, skip);
        const std::size_t bytes =
            static_cast<std::size_t>(origin.w) * static_cast<std::size_t>(origin.h) * 4;
        if (!hole_cache_valid_ || hole_cache_origin_.x != origin.x ||
            hole_cache_origin_.y != origin.y || hole_cache_origin_.w != origin.w ||
            hole_cache_origin_.h != origin.h || hole_cache_skip_ != skip || hole_cache_fp_ != fp ||
            hole_cache_.size() != bytes) {
          hole_cache_.assign(bytes, 0);
          if (!origin.empty()) {
            document_->layers().composite_rect(hole_cache_.data(), origin.w * 4, origin, nullptr, -1,
                                                skip);
          }
          hole_cache_origin_ = origin;
          hole_cache_skip_ = skip;
          hole_cache_fp_ = fp;
          hole_cache_valid_ = true;
        }
        hole_rgba = hole_cache_.data();
        hole_stride = origin.w * 4;
      }
      paint_floating_selection(document_->layers(), sel, dst, dst_stride, view, true, hole_clear,
                               float_clear, hole_rgba, hole_stride);
    }
  } else {
    hole_cache_valid_ = false;
  }
  premultiply_rows(dst, out_w, out_h, dst_stride);
  blit_surface_->mark_dirty();
  cr->save();
  cr->translate(ox + view.x * zoom_, oy + view.y * zoom_);
  if (downscale) {
    cr->scale((view.w * zoom_) / static_cast<double>(out_w),
              (view.h * zoom_) / static_cast<double>(out_h));
  } else {
    cr->scale(zoom_, zoom_);
  }
  cr->set_source(blit_surface_, 0, 0);
  if (Cairo::RefPtr<Cairo::Pattern> src = cr->get_source()) {
    cairo_pattern_set_filter(src->cobj(), CAIRO_FILTER_NEAREST);
  }
  cr->paint();
  cr->restore();
  (void)cw;
  (void)ch;

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
  if (tool_ != nullptr) {
    tool_->draw_overlay(cr, ox, oy, zoom_);
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
    update_pan(event->x_root, event->y_root);
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
  const bool hand_tool = tool_ != nullptr && tool_->id() != nullptr &&
                         std::string(tool_->id()) == "hand";
  if (event->button == 2 || (event->button == 1 && space_down_) ||
      (event->button == 1 && hand_tool)) {
    begin_pan(event->x_root, event->y_root);
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
  checker_pattern_.clear();
  invalidate_all();
}

void CanvasView::rebuild_checker() {
  auto tile = Cairo::ImageSurface::create(Cairo::FORMAT_RGB24, 16, 16);
  tile->flush();
  fill_checker_tile(tile->get_data(), tile->get_stride(), checker_light_, checker_dark_);
  tile->mark_dirty();
  checker_pattern_ = Cairo::SurfacePattern::create(tile);
  checker_pattern_->set_extend(Cairo::EXTEND_REPEAT);
  checker_pattern_->set_filter(Cairo::FILTER_NEAREST);
}

void CanvasView::set_grid_threshold(int percent) {
  if (percent < 100) {
    percent = 100;
  }
  grid_threshold_ = percent;
  invalidate_all();
}

Rect CanvasView::viewport_canvas_rect(int clip_x0, int clip_y0, int clip_x1, int clip_y1) const {
  const int cw = canvas_width();
  const int ch = canvas_height();
  if (cw < 1 || ch < 1) {
    return {};
  }
  const auto hadj = get_hadjustment();
  const auto vadj = get_vadjustment();
  double page_w = hadj ? hadj->get_page_size() : 0.0;
  double page_h = vadj ? vadj->get_page_size() : 0.0;
  if (page_w < 2.0) {
    page_w = static_cast<double>(get_allocated_width());
  }
  if (page_h < 2.0) {
    page_h = static_cast<double>(get_allocated_height());
  }
  Rect view{};
  if (page_w >= 2.0 && page_h >= 2.0) {
    const double scroll_x = hadj ? hadj->get_value() : 0.0;
    const double scroll_y = vadj ? vadj->get_value() : 0.0;
    const double vx0 = scroll_x - static_cast<double>(area_move_x_);
    const double vy0 = scroll_y - static_cast<double>(area_move_y_);
    const double z = std::max(zoom_, 1e-6);
    const int ox = origin_x();
    const int oy = origin_y();
    int x0 = static_cast<int>(std::floor((vx0 - ox) / z)) - 1;
    int y0 = static_cast<int>(std::floor((vy0 - oy) / z)) - 1;
    int x1 = static_cast<int>(std::ceil((vx0 + page_w - ox) / z)) + 1;
    int y1 = static_cast<int>(std::ceil((vy0 + page_h - oy) / z)) + 1;
    x0 = std::clamp(x0, 0, cw);
    y0 = std::clamp(y0, 0, ch);
    x1 = std::clamp(x1, 0, cw);
    y1 = std::clamp(y1, 0, ch);
    if (x1 > x0 && y1 > y0) {
      view = Rect{x0, y0, x1 - x0, y1 - y0};
    }
  }
  if (view.empty() && clip_x1 > clip_x0 && clip_y1 > clip_y0) {
    view = Rect{clip_x0, clip_y0, clip_x1 - clip_x0, clip_y1 - clip_y0};
  }
  return view;
}

void CanvasView::ensure_composite(Rect view, int out_w, int out_h, bool downscale,
                                  const Layer* tool_override, int tool_index) {
  const bool tool_on = tool_override != nullptr;
  const bool same = composite_valid_ && !composite_dirty_all_ && composite_view_.x == view.x &&
                    composite_view_.y == view.y && composite_view_.w == view.w &&
                    composite_view_.h == view.h && composite_out_w_ == out_w &&
                    composite_out_h_ == out_h && composite_downscale_ == downscale &&
                    composite_tool_ == tool_on && composite_tool_index_ == tool_index &&
                    std::abs(composite_zoom_ - zoom_) < 1e-9 &&
                    composite_cache_.size() ==
                        static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4;
  auto remember = [&]() {
    composite_view_ = view;
    composite_out_w_ = out_w;
    composite_out_h_ = out_h;
    composite_downscale_ = downscale;
    composite_tool_ = tool_on;
    composite_tool_index_ = tool_index;
    composite_zoom_ = zoom_;
    composite_valid_ = true;
    composite_dirty_all_ = false;
    composite_dirty_ = {};
  };
  const int stride = out_w * 4;
  if (!same) {
    composite_cache_.assign(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4, 0);
    if (downscale) {
      document_->layers().composite_scaled(composite_cache_.data(), stride, view, out_w, out_h,
                                           tool_override, tool_index);
    } else {
      document_->layers().composite_rect(composite_cache_.data(), stride, view, tool_override,
                                         tool_index);
    }
    remember();
    return;
  }
  if (composite_dirty_.empty()) {
    return;
  }
  const Rect dirty = rect_intersect(composite_dirty_, view);
  composite_dirty_ = {};
  if (dirty.empty()) {
    return;
  }
  if (downscale) {
    const int x0 = std::clamp(
        static_cast<int>((static_cast<long long>(dirty.x - view.x) * out_w) / std::max(1, view.w)), 0,
        out_w);
    const int y0 = std::clamp(
        static_cast<int>((static_cast<long long>(dirty.y - view.y) * out_h) / std::max(1, view.h)), 0,
        out_h);
    const int x1 = std::clamp(static_cast<int>((static_cast<long long>(dirty.x2() - view.x) * out_w +
                                                view.w - 1) /
                                               std::max(1, view.w)),
                              0, out_w);
    const int y1 = std::clamp(static_cast<int>((static_cast<long long>(dirty.y2() - view.y) * out_h +
                                                view.h - 1) /
                                               std::max(1, view.h)),
                              0, out_h);
    if (x1 <= x0 || y1 <= y0) {
      return;
    }
    document_->layers().composite_scaled(composite_cache_.data(), stride, view, out_w, out_h,
                                         tool_override, tool_index, -1, Rect{x0, y0, x1 - x0, y1 - y0});
    return;
  }
  std::uint8_t* dest = composite_cache_.data() +
                       static_cast<std::size_t>(dirty.y - view.y) * static_cast<std::size_t>(stride) +
                       static_cast<std::size_t>(dirty.x - view.x) * 4;
  document_->layers().composite_rect(dest, stride, dirty, tool_override, tool_index);
}

void CanvasView::paint_scaled_float(std::uint8_t* dest, int dest_stride, Rect view, int out_w,
                                    int out_h) {
  if (document_ == nullptr || dest == nullptr || view.empty()) {
    return;
  }
  const Selection& sel = document_->selection();
  if (!sel.floating()) {
    return;
  }
  const Color canvas_bg = document_->canvas_background();
  const Color hole_clear = canvas_bg.a != 0 ? canvas_bg : Color::white();
  auto nearest = [&](int ox, int oy, int& sx, int& sy) {
    sx = view.x + static_cast<int>((static_cast<long long>(ox) * view.w + view.w / 2) / std::max(1, out_w));
    sy = view.y + static_cast<int>((static_cast<long long>(oy) * view.h + view.h / 2) / std::max(1, out_h));
    sx = std::clamp(sx, view.x, view.x2() - 1);
    sy = std::clamp(sy, view.y, view.y2() - 1);
  };
  auto out_span = [&](Rect canvas, int& x0, int& y0, int& x1, int& y1) {
    const Rect r = rect_intersect(canvas, view);
    if (r.empty()) {
      x0 = y0 = x1 = y1 = 0;
      return;
    }
    x0 = std::clamp(static_cast<int>((static_cast<long long>(r.x - view.x) * out_w) / std::max(1, view.w)),
                    0, out_w);
    y0 = std::clamp(static_cast<int>((static_cast<long long>(r.y - view.y) * out_h) / std::max(1, view.h)),
                    0, out_h);
    x1 = std::clamp(static_cast<int>((static_cast<long long>(r.x2() - view.x) * out_w + view.w - 1) /
                                     std::max(1, view.w)),
                    0, out_w);
    y1 = std::clamp(static_cast<int>((static_cast<long long>(r.y2() - view.y) * out_h + view.h - 1) /
                                     std::max(1, view.h)),
                    0, out_h);
  };
  if (!sel.copy_mode()) {
    int skip = sel.source_layer();
    if (skip < 0 || skip >= document_->layers().count()) {
      skip = document_->layers().active_index();
    }
    const Rect origin = sel.origin_rect();
    int x0 = 0;
    int y0 = 0;
    int x1 = 0;
    int y1 = 0;
    out_span(origin, x0, y0, x1, y1);
    if (x1 > x0 && y1 > y0) {
      std::vector<std::uint8_t> patch(static_cast<std::size_t>(out_w) * static_cast<std::size_t>(out_h) * 4,
                                      0);
      document_->layers().composite_scaled(patch.data(), out_w * 4, view, out_w, out_h, nullptr, -1, skip,
                                           Rect{x0, y0, x1 - x0, y1 - y0});
      for (int oy = y0; oy < y1; ++oy) {
        for (int ox = x0; ox < x1; ++ox) {
          int sx = 0;
          int sy = 0;
          nearest(ox, oy, sx, sy);
          if (!sel.origin_covers(sx - origin.x, sy - origin.y)) {
            continue;
          }
          const std::uint8_t* s = patch.data() +
                                  (static_cast<std::size_t>(oy) * static_cast<std::size_t>(out_w) +
                                   static_cast<std::size_t>(ox)) *
                                      4;
          std::uint8_t* d = dest + static_cast<std::size_t>(oy) * static_cast<std::size_t>(dest_stride) +
                            static_cast<std::size_t>(ox) * 4;
          if (s[3] == 0) {
            d[0] = hole_clear.r;
            d[1] = hole_clear.g;
            d[2] = hole_clear.b;
            d[3] = hole_clear.a;
          } else {
            std::memcpy(d, s, 4);
          }
        }
      }
    }
  }
  const Rect fr = sel.float_rect();
  int x0 = 0;
  int y0 = 0;
  int x1 = 0;
  int y1 = 0;
  out_span(fr, x0, y0, x1, y1);
  if (sel.float_pixels() == nullptr || x1 <= x0 || y1 <= y0) {
    return;
  }
  for (int oy = y0; oy < y1; ++oy) {
    for (int ox = x0; ox < x1; ++ox) {
      int sx = 0;
      int sy = 0;
      nearest(ox, oy, sx, sy);
      const int lx = sx - fr.x;
      const int ly = sy - fr.y;
      if (!sel.float_covers(lx, ly)) {
        continue;
      }
      const std::uint8_t* s = sel.float_pixels() +
                              (static_cast<std::size_t>(ly) * static_cast<std::size_t>(fr.w) +
                               static_cast<std::size_t>(lx)) *
                                  4;
      if (s[3] == 0) {
        continue;
      }
      std::uint8_t* d = dest + static_cast<std::size_t>(oy) * static_cast<std::size_t>(dest_stride) +
                        static_cast<std::size_t>(ox) * 4;
      if (s[3] == 255) {
        std::memcpy(d, s, 4);
      } else {
        blend_pixel(d, s, BlendMode::Normal, 1.0f);
      }
    }
  }
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
  double cx = 0;
  double cy = 0;
  visible_center(cx, cy);
  zoom_to(zoom_fit_ratio(vw, vh, cw, ch), cx, cy, false);
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

void CanvasView::rebuild_ants() {
  ants_path_.reset();
  ants_halos_.clear();
  ants_generation_ = 0;
  if (document_ == nullptr || document_->selection().empty()) {
    return;
  }
  const Selection& sel = document_->selection();
  ants_generation_ = sel.generation();
  auto scratch = Cairo::ImageSurface::create(Cairo::FORMAT_A1, 1, 1);
  auto cr = Cairo::Context::create(scratch);
  const int pad = std::max(1, static_cast<int>(std::ceil(1.0 / std::max(zoom_, 0.125))));
  auto add_halo = [&](Rect r) {
    if (r.empty()) {
      return;
    }
    const Rect h{r.x - pad, r.y - pad, r.w + pad * 2, r.h + pad * 2};
    const int band = pad * 2 + 1;
    ants_halos_.push_back(Rect{h.x, h.y, h.w, band});
    ants_halos_.push_back(Rect{h.x, h.y + h.h - band, h.w, band});
    ants_halos_.push_back(Rect{h.x, h.y, band, h.h});
    ants_halos_.push_back(Rect{h.x + h.w - band, h.y, band, h.h});
    cr->rectangle(r.x, r.y, r.w, r.h);
  };

  auto add_mask = [&](bool* used_bounds) {
    if (used_bounds != nullptr) {
      *used_bounds = false;
    }
    if (!sel.has_mask() || sel.mask() == nullptr) {
      add_halo(sel.bounds());
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
    int spans = 0;
    for (int y = 0; y < mh && spans <= 512; ++y) {
      for (int x = 0; x < mw && spans <= 512;) {
        if (inside(x, y) && !inside(x, y - 1)) {
          const int x0 = x;
          while (x < mw && inside(x, y) && !inside(x, y - 1)) {
            ++x;
          }
          cr->move_to(b.x + x0, b.y + y);
          cr->line_to(b.x + x, b.y + y);
          ++spans;
        } else if (inside(x, y) && !inside(x, y + 1)) {
          const int x0 = x;
          while (x < mw && inside(x, y) && !inside(x, y + 1)) {
            ++x;
          }
          cr->move_to(b.x + x0, b.y + y + 1);
          cr->line_to(b.x + x, b.y + y + 1);
          ++spans;
        } else {
          ++x;
        }
      }
    }
    for (int x = 0; x < mw && spans <= 512; ++x) {
      for (int y = 0; y < mh && spans <= 512;) {
        if (inside(x, y) && !inside(x - 1, y)) {
          const int y0 = y;
          while (y < mh && inside(x, y) && !inside(x - 1, y)) {
            ++y;
          }
          cr->move_to(b.x + x, b.y + y0);
          cr->line_to(b.x + x, b.y + y);
          ++spans;
        } else if (inside(x, y) && !inside(x + 1, y)) {
          const int y0 = y;
          while (y < mh && inside(x, y) && !inside(x + 1, y)) {
            ++y;
          }
          cr->move_to(b.x + x + 1, b.y + y0);
          cr->line_to(b.x + x + 1, b.y + y);
          ++spans;
        } else {
          ++y;
        }
      }
    }
    if (spans > 512) {
      if (used_bounds != nullptr) {
        *used_bounds = true;
      }
      cr->begin_new_path();
      if (sel.inverted()) {
        add_halo(Rect{0, 0, document_->width(), document_->height()});
      }
      add_halo(b);
      return;
    }
    const Rect h{b.x - pad, b.y - pad, b.w + pad * 2, b.h + pad * 2};
    const int band = pad * 2 + 1;
    ants_halos_.push_back(Rect{h.x, h.y, h.w, band});
    ants_halos_.push_back(Rect{h.x, h.y + h.h - band, h.w, band});
    ants_halos_.push_back(Rect{h.x, h.y, band, h.h});
    ants_halos_.push_back(Rect{h.x + h.w - band, h.y, band, h.h});
  };

  if (sel.inverted()) {
    add_halo(Rect{0, 0, document_->width(), document_->height()});
  }
  if (sel.has_mask() && !sel.floating()) {
    add_mask(nullptr);
  } else if (!sel.inverted() || !sel.bounds().empty()) {
    if (!(sel.has_mask() && !sel.floating())) {
      add_halo(sel.bounds());
    }
  }
  ants_path_.reset(cr->copy_path());
}

void CanvasView::draw_marching_ants(const Cairo::RefPtr<Cairo::Context>& cr, int ox, int oy) {
  if (document_ == nullptr || document_->selection().empty()) {
    ants_path_.reset();
    ants_halos_.clear();
    return;
  }
  const Selection& sel = document_->selection();
  if (!ants_path_ || ants_generation_ != sel.generation()) {
    rebuild_ants();
  }
  if (!ants_path_) {
    return;
  }
  const double z = std::max(zoom_, 0.125);
  const double dash = 4.0 / z;
  cr->save();
  cr->translate(ox, oy);
  cr->scale(zoom_, zoom_);
  cr->set_line_width(1.0 / z);
  std::vector<double> dashes{dash, dash};
  cr->set_dash(dashes, static_cast<double>(ants_phase_) / z);
  cr->set_source_rgb(0.0, 0.0, 0.0);
  cr->append_path(*ants_path_);
  cr->stroke();
  cr->set_dash(dashes, static_cast<double>(ants_phase_) / z + dash);
  cr->set_source_rgb(1.0, 1.0, 1.0);
  cr->append_path(*ants_path_);
  cr->stroke();
  cr->restore();
}

void CanvasView::ensure_ants_timer() {
  if (document_ == nullptr || document_->selection().empty()) {
    if (ants_timer_.connected()) {
      ants_timer_.disconnect();
    }
    return;
  }
  if (ants_timer_.connected()) {
    return;
  }
  ants_timer_ = Glib::signal_timeout().connect(
      [this]() {
        if (document_ == nullptr || document_->selection().empty()) {
          return false;
        }
        ants_phase_ = (ants_phase_ + 1) % 8;
        invalidate_ants();
        return true;
      },
      90);
}

void CanvasView::invalidate_ants() {
  if (document_ == nullptr || document_->selection().empty()) {
    if (ants_timer_.connected()) {
      ants_timer_.disconnect();
    }
    return;
  }
  if (ants_halos_.empty() || ants_generation_ != document_->selection().generation()) {
    rebuild_ants();
  }
  for (const Rect& halo : ants_halos_) {
    queue_canvas_area(halo);
  }
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
  ants_path_.reset();
  invalidate_all();
  signal_view_changed_.emit();
}

}  // namespace lundukepaint
