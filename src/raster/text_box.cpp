// SPDX-License-Identifier: GPL-3.0-or-later

#include "raster/text_box.hpp"

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/selection.hpp"
#include "raster/text.hpp"

#include <cairo.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace lundukepaint {
namespace {

struct Layout {
  cairo_surface_t* surface = nullptr;
  cairo_t* cr = nullptr;
  PangoLayout* layout = nullptr;

  ~Layout() {
    if (layout != nullptr) {
      g_object_unref(layout);
    }
    if (cr != nullptr) {
      cairo_destroy(cr);
    }
    if (surface != nullptr) {
      cairo_surface_destroy(surface);
    }
  }

  Layout() = default;
  Layout(const Layout&) = delete;
  Layout& operator=(const Layout&) = delete;
};

int clamp_cursor(const TextBoxState& state) {
  if (state.cursor <= 0 || state.text.empty()) {
    return 0;
  }
  if (state.cursor >= static_cast<int>(state.text.size())) {
    return static_cast<int>(state.text.size());
  }
  const char* start = state.text.c_str();
  const char* end = start + state.text.size();
  const char* cur = start + state.cursor;
  // Walk back to a UTF-8 boundary if a previous edit left the caret mid-sequence.
  while (cur > start && (static_cast<unsigned char>(*cur) & 0xc0) == 0x80) {
    --cur;
  }
  if (cur >= end) {
    return static_cast<int>(state.text.size());
  }
  return static_cast<int>(cur - start);
}

bool open_layout(const TextBoxState& state, Layout& out) {
  out.surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 8, 8);
  if (cairo_surface_status(out.surface) != CAIRO_STATUS_SUCCESS) {
    return false;
  }
  out.cr = cairo_create(out.surface);
  if (cairo_status(out.cr) != CAIRO_STATUS_SUCCESS) {
    return false;
  }
  out.layout = pango_cairo_create_layout(out.cr);
  if (out.layout == nullptr) {
    return false;
  }
  const std::string& family = state.family.empty() ? std::string("Sans") : state.family;
  PangoFontDescription* desc = pango_font_description_new();
  pango_font_description_set_family(desc, family.c_str());
  pango_font_description_set_size(desc, std::max(1, state.size_pt) * PANGO_SCALE);
  pango_font_description_set_weight(desc, state.bold ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL);
  pango_font_description_set_style(desc, state.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
  pango_layout_set_font_description(out.layout, desc);
  pango_font_description_free(desc);
  pango_layout_set_text(out.layout, state.text.c_str(), static_cast<int>(state.text.size()));
  const int wrap = text_box_content_width(state);
  pango_layout_set_width(out.layout, wrap * PANGO_SCALE);
  pango_layout_set_wrap(out.layout, PANGO_WRAP_WORD_CHAR);
  return true;
}

void fill_metrics(PangoLayout* layout, const TextBoxState& state, TextBoxMetrics& metrics) {
  metrics = {};
  if (layout == nullptr) {
    metrics.cursor_h = std::max(1, state.size_pt);
    return;
  }
  metrics.line_count = std::max(1, pango_layout_get_line_count(layout));
  pango_layout_get_pixel_size(layout, &metrics.layout_width, &metrics.layout_height);
  if (metrics.layout_width < 1) {
    metrics.layout_width = 1;
  }
  if (metrics.layout_height < 1) {
    metrics.layout_height = std::max(1, state.size_pt);
  }
  const int cursor = clamp_cursor(state);
  PangoRectangle strong;
  std::memset(&strong, 0, sizeof(strong));
  pango_layout_get_cursor_pos(layout, cursor, &strong, nullptr);
  metrics.cursor_x = strong.x / PANGO_SCALE;
  metrics.cursor_y = strong.y / PANGO_SCALE;
  metrics.cursor_h = std::max(1, strong.height / PANGO_SCALE);
}

bool near_point(int x, int y, int px, int py, int radius) {
  return std::abs(x - px) <= radius && std::abs(y - py) <= radius;
}

void handle_points(const Rect& box, int* xs, int* ys) {
  xs[0] = box.x;
  ys[0] = box.y;
  xs[1] = box.x + box.w / 2;
  ys[1] = box.y;
  xs[2] = box.x + box.w;
  ys[2] = box.y;
  xs[3] = box.x + box.w;
  ys[3] = box.y + box.h / 2;
  xs[4] = box.x + box.w;
  ys[4] = box.y + box.h;
  xs[5] = box.x + box.w / 2;
  ys[5] = box.y + box.h;
  xs[6] = box.x;
  ys[6] = box.y + box.h;
  xs[7] = box.x;
  ys[7] = box.y + box.h / 2;
}

void move_vertical(TextBoxState& state, int direction) {
  Layout layout;
  if (!open_layout(state, layout)) {
    return;
  }
  const int cursor = clamp_cursor(state);
  PangoRectangle strong;
  std::memset(&strong, 0, sizeof(strong));
  pango_layout_get_cursor_pos(layout.layout, cursor, &strong, nullptr);
  const int y = direction < 0 ? strong.y - PANGO_SCALE : strong.y + strong.height + PANGO_SCALE;
  int index = 0;
  int trailing = 0;
  pango_layout_xy_to_index(layout.layout, strong.x, y, &index, &trailing);
  if (trailing != 0 && index >= 0 && index < static_cast<int>(state.text.size())) {
    const char* next = g_utf8_next_char(state.text.c_str() + index);
    index = static_cast<int>(next - state.text.c_str());
  }
  if (index < 0) {
    index = 0;
  }
  if (index > static_cast<int>(state.text.size())) {
    index = static_cast<int>(state.text.size());
  }
  state.cursor = index;
}

void move_line_edge(TextBoxState& state, bool to_end) {
  Layout layout;
  if (!open_layout(state, layout)) {
    return;
  }
  int line = 0;
  int x_off = 0;
  pango_layout_index_to_line_x(layout.layout, clamp_cursor(state), FALSE, &line, &x_off);
  PangoLayoutLine* ln = pango_layout_get_line_readonly(layout.layout, line);
  if (ln == nullptr) {
    return;
  }
  if (!to_end) {
    state.cursor = ln->start_index;
    return;
  }
  int end = ln->start_index + ln->length;
  if (end > ln->start_index && end <= static_cast<int>(state.text.size())) {
    if (state.text[static_cast<std::size_t>(end - 1)] == '\n') {
      --end;
    }
    if (end > ln->start_index && state.text[static_cast<std::size_t>(end - 1)] == '\r') {
      --end;
    }
  }
  state.cursor = end;
}

}  // namespace

int text_box_handle_radius(double zoom) {
  if (zoom < 0.25) {
    zoom = 0.25;
  }
  return std::max(3, static_cast<int>(std::lround(6.0 / zoom)));
}

int text_box_border_slop(double zoom) {
  if (zoom < 0.25) {
    zoom = 0.25;
  }
  return std::max(2, static_cast<int>(std::lround(4.0 / zoom)));
}

TextBoxHit hit_test_text_box(const Rect& box, int x, int y, double zoom) {
  if (box.w < 1 || box.h < 1) {
    return TextBoxHit::Outside;
  }
  const int radius = text_box_handle_radius(zoom);
  int xs[8];
  int ys[8];
  handle_points(box, xs, ys);
  static const TextBoxHit kHits[8] = {
      TextBoxHit::NorthWest, TextBoxHit::North,     TextBoxHit::NorthEast, TextBoxHit::East,
      TextBoxHit::SouthEast, TextBoxHit::South,     TextBoxHit::SouthWest, TextBoxHit::West};
  for (int i = 0; i < 8; ++i) {
    if (near_point(x, y, xs[i], ys[i], radius)) {
      return kHits[i];
    }
  }
  const int slop = text_box_border_slop(zoom);
  const int left = box.x;
  const int top = box.y;
  const int right = box.x + box.w;
  const int bottom = box.y + box.h;
  const bool in_outer = x >= left - slop && y >= top - slop && x <= right + slop && y <= bottom + slop;
  if (!in_outer) {
    return TextBoxHit::Outside;
  }
  const int inner_l = left + slop;
  const int inner_t = top + slop;
  const int inner_r = right - slop;
  const int inner_b = bottom - slop;
  const bool in_inner = inner_r > inner_l && inner_b > inner_t && x >= inner_l && y >= inner_t &&
                        x <= inner_r && y <= inner_b;
  return in_inner ? TextBoxHit::Inside : TextBoxHit::Border;
}

TextBoxPress text_box_press_action(TextBoxHit hit) {
  switch (hit) {
    case TextBoxHit::Outside:
      return TextBoxPress::Commit;
    case TextBoxHit::Inside:
      return TextBoxPress::PlaceCursor;
    case TextBoxHit::Border:
      return TextBoxPress::Move;
    default:
      return TextBoxPress::Resize;
  }
}

void move_text_box(Rect& box, int dx, int dy) {
  box.x += dx;
  box.y += dy;
}

void resize_text_box(Rect& box, TextBoxHit handle, int pointer_x, int pointer_y, const Rect& start,
                     int grab_x, int grab_y) {
  const int dx = pointer_x - grab_x;
  const int dy = pointer_y - grab_y;
  int left = start.x;
  int top = start.y;
  int right = start.x + start.w;
  int bottom = start.y + start.h;
  const bool east = handle == TextBoxHit::East || handle == TextBoxHit::NorthEast ||
                    handle == TextBoxHit::SouthEast;
  const bool west = handle == TextBoxHit::West || handle == TextBoxHit::NorthWest ||
                    handle == TextBoxHit::SouthWest;
  const bool south = handle == TextBoxHit::South || handle == TextBoxHit::SouthEast ||
                     handle == TextBoxHit::SouthWest;
  const bool north = handle == TextBoxHit::North || handle == TextBoxHit::NorthEast ||
                     handle == TextBoxHit::NorthWest;
  if (east) {
    right = std::max(left + kTextBoxMinSize, right + dx);
  } else if (west) {
    left = std::min(right - kTextBoxMinSize, left + dx);
  }
  if (south) {
    bottom = std::max(top + kTextBoxMinSize, bottom + dy);
  } else if (north) {
    top = std::min(bottom - kTextBoxMinSize, top + dy);
  }
  box = Rect{left, top, right - left, bottom - top};
}

int text_box_content_width(const TextBoxState& state) {
  return std::max(1, state.box.w - 2 * kTextBoxPad);
}

int text_box_content_height(const TextBoxState& state) {
  return std::max(1, state.box.h - 2 * kTextBoxPad);
}

void layout_text_box(const TextBoxState& state, TextBoxMetrics& metrics) {
  Layout layout;
  if (!open_layout(state, layout)) {
    fill_metrics(nullptr, state, metrics);
    return;
  }
  fill_metrics(layout.layout, state, metrics);
}

bool render_text_box(const TextBoxState& state, std::vector<std::uint8_t>& rgba, int& width,
                     int& height, TextBoxMetrics* metrics) {
  rgba.clear();
  width = 0;
  height = 0;
  TextBoxMetrics local;
  TextBoxMetrics& out_metrics = metrics != nullptr ? *metrics : local;
  if (state.size_pt < 1) {
    fill_metrics(nullptr, state, out_metrics);
    return false;
  }
  Layout measure;
  if (!open_layout(state, measure)) {
    fill_metrics(nullptr, state, out_metrics);
    return false;
  }
  fill_metrics(measure.layout, state, out_metrics);
  if (state.text.empty() || state.color.a == 0) {
    return false;
  }
  const int tw = std::max(1, out_metrics.layout_width);
  const int full_h = std::max(1, out_metrics.layout_height);
  const int th = std::min(full_h, text_box_content_height(state));
  cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, tw, full_h);
  if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
    cairo_surface_destroy(surface);
    return false;
  }
  cairo_t* cr = cairo_create(surface);
  cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
  cairo_set_source_rgba(cr, 0, 0, 0, 0);
  cairo_paint(cr);
  cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
  cairo_set_source_rgba(cr, state.color.r / 255.0, state.color.g / 255.0, state.color.b / 255.0,
                        state.color.a / 255.0);
  pango_cairo_update_layout(cr, measure.layout);
  pango_cairo_show_layout(cr, measure.layout);
  cairo_surface_flush(surface);
  const int stride = cairo_image_surface_get_stride(surface);
  const std::uint8_t* src = cairo_image_surface_get_data(surface);
  rgba.assign(static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4, 0);
  if (src != nullptr) {
    for (int y = 0; y < th; ++y) {
      const std::uint8_t* srow = src + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
      std::uint8_t* drow = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(tw) * 4;
      for (int x = 0; x < tw; ++x) {
        const std::uint8_t* p = srow + static_cast<std::size_t>(x) * 4;
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
        const int a = p[3];
        const int b = p[0];
        const int g = p[1];
        const int r = p[2];
#else
        const int a = p[0];
        const int r = p[1];
        const int g = p[2];
        const int b = p[3];
#endif
        std::uint8_t* d = drow + static_cast<std::size_t>(x) * 4;
        if (a == 0) {
          d[0] = d[1] = d[2] = d[3] = 0;
          continue;
        }
        d[0] = static_cast<std::uint8_t>(r * 255 / a);
        d[1] = static_cast<std::uint8_t>(g * 255 / a);
        d[2] = static_cast<std::uint8_t>(b * 255 / a);
        d[3] = static_cast<std::uint8_t>(a);
      }
    }
  }
  cairo_destroy(cr);
  cairo_surface_destroy(surface);
  width = tw;
  height = th;
  return true;
}

int text_box_line_count(const TextBoxState& state) {
  TextBoxMetrics metrics;
  layout_text_box(state, metrics);
  return metrics.line_count;
}

int text_box_index_at(const TextBoxState& state, int canvas_x, int canvas_y) {
  if (state.text.empty()) {
    return 0;
  }
  Layout layout;
  if (!open_layout(state, layout)) {
    return clamp_cursor(state);
  }
  const int lx = (canvas_x - (state.box.x + kTextBoxPad)) * PANGO_SCALE;
  const int ly = (canvas_y - (state.box.y + kTextBoxPad)) * PANGO_SCALE;
  int index = 0;
  int trailing = 0;
  pango_layout_xy_to_index(layout.layout, lx, ly, &index, &trailing);
  if (trailing != 0 && index >= 0 && index < static_cast<int>(state.text.size())) {
    const char* next = g_utf8_next_char(state.text.c_str() + index);
    index = static_cast<int>(next - state.text.c_str());
  }
  if (index < 0) {
    return 0;
  }
  if (index > static_cast<int>(state.text.size())) {
    return static_cast<int>(state.text.size());
  }
  return index;
}

void text_box_insert(TextBoxState& state, const std::string& utf8) {
  if (utf8.empty()) {
    return;
  }
  const int cursor = clamp_cursor(state);
  state.text.insert(static_cast<std::size_t>(cursor), utf8);
  state.cursor = cursor + static_cast<int>(utf8.size());
}

void text_box_backspace(TextBoxState& state) {
  const int cursor = clamp_cursor(state);
  if (cursor <= 0 || state.text.empty()) {
    state.cursor = 0;
    return;
  }
  const char* start = state.text.c_str();
  const char* cur = start + cursor;
  const char* prev = g_utf8_find_prev_char(start, cur);
  if (prev == nullptr) {
    prev = start;
  }
  const int at = static_cast<int>(prev - start);
  state.text.erase(static_cast<std::size_t>(at), static_cast<std::size_t>(cursor - at));
  state.cursor = at;
}

void text_box_delete_forward(TextBoxState& state) {
  const int cursor = clamp_cursor(state);
  if (cursor >= static_cast<int>(state.text.size())) {
    state.cursor = static_cast<int>(state.text.size());
    return;
  }
  const char* cur = state.text.c_str() + cursor;
  const char* next = g_utf8_next_char(cur);
  const int end = static_cast<int>(next - state.text.c_str());
  state.text.erase(static_cast<std::size_t>(cursor), static_cast<std::size_t>(end - cursor));
  state.cursor = cursor;
}

void text_box_move_left(TextBoxState& state) {
  const int cursor = clamp_cursor(state);
  if (cursor <= 0) {
    state.cursor = 0;
    return;
  }
  const char* start = state.text.c_str();
  const char* prev = g_utf8_find_prev_char(start, start + cursor);
  state.cursor = prev != nullptr ? static_cast<int>(prev - start) : 0;
}

void text_box_move_right(TextBoxState& state) {
  const int cursor = clamp_cursor(state);
  if (cursor >= static_cast<int>(state.text.size())) {
    state.cursor = static_cast<int>(state.text.size());
    return;
  }
  const char* next = g_utf8_next_char(state.text.c_str() + cursor);
  state.cursor = static_cast<int>(next - state.text.c_str());
}

void text_box_move_up(TextBoxState& state) {
  move_vertical(state, -1);
}

void text_box_move_down(TextBoxState& state) {
  move_vertical(state, 1);
}

void text_box_move_line_start(TextBoxState& state) {
  move_line_edge(state, false);
}

void text_box_move_line_end(TextBoxState& state) {
  move_line_edge(state, true);
}

bool commit_text_box(Document& document, const TextBoxState& state) {
  if (state.text.empty() || state.size_pt < 1 || document.active_locked()) {
    return false;
  }
  std::vector<std::uint8_t> rgba;
  int tw = 0;
  int th = 0;
  if (!render_text_box(state, rgba, tw, th, nullptr) || rgba.empty() || tw < 1 || th < 1) {
    return false;
  }
  Layer& active = document.layers().active_layer();
  document.layers().copy_active_to_tool();
  Layer& tool = document.layers().tool_layer();
  const int dx = state.box.x + kTextBoxPad - active.offset_x();
  const int dy = state.box.y + kTextBoxPad - active.offset_y();
  Rect dirty{};
  blit_rgba_buffer(tool.pixels(), tool.width(), tool.height(), tool.stride(), dx, dy, rgba.data(),
                   tw, th, tw * 4, true, &dirty);
  clip_rect_to_selection(tool, active, dirty, document.selection());
  auto cmd = PixelPatchCommand::from_layers(active, tool, dirty, "Text", document.layers().active_index());
  document.layers().clear_tool_layer();
  if (cmd && !cmd->empty()) {
    document.commit(std::move(cmd));
    return true;
  }
  return false;
}

}  // namespace lundukepaint
