// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_TOOLS_TOOL_HPP
#define LUNDUKEPAINT_TOOLS_TOOL_HPP

#include "raster/types.hpp"
#include "raster/shapes.hpp"
#include "raster/pattern.hpp"

#include <cairomm/context.h>

#include <string>

namespace Gtk {
class Widget;
class Window;
}

namespace lundukepaint {

class Document;

struct CanvasEvent {
  double x = 0;
  double y = 0;
  unsigned button = 0;      // 1 left, 2 middle, 3 right
  unsigned modifiers = 0;   // Modifier bits below
};

struct Modifier {
  static constexpr unsigned Shift = 1u;
  static constexpr unsigned Ctrl = 2u;
  static constexpr unsigned Alt = 4u;
};

class ToolHost {
public:
  virtual ~ToolHost() = default;
  virtual Document& document() = 0;
  virtual int stroke_size() const = 0;
  virtual void set_stroke_size(int size) = 0;
  virtual bool brush_antialias() const = 0;
  virtual void set_brush_antialias(bool enabled) = 0;
  virtual int fill_tolerance() const = 0;
  virtual void set_fill_tolerance(int tolerance) = 0;
  virtual void invalidate_canvas(Rect rect) = 0;
  virtual void return_to_previous_tool() = 0;
  virtual Color sample_canvas(int x, int y) const = 0;
  virtual void show_status_hint(const char* message) = 0;
  virtual bool canvas_to_screen(int canvas_x, int canvas_y, int& screen_x, int& screen_y) {
    (void)canvas_x;
    (void)canvas_y;
    screen_x = 0;
    screen_y = 0;
    return false;
  }
  virtual double canvas_zoom() const { return 1.0; }
  // Toplevel to parent transient popups on (the text tool's entry).
  virtual Gtk::Window* host_window() { return nullptr; }
  virtual int pattern_index() const { return 0; }
  virtual void set_pattern_index(int /*index*/) {}
  virtual const Pattern& active_pattern() const { return pattern_at(0); }
  virtual int brush_tip() const { return 3; }
  virtual void set_brush_tip(int /*index*/) {}
  virtual int spray_radius() const { return 16; }
  virtual void set_spray_radius(int /*radius*/) {}
};


class Tool {
public:
  virtual ~Tool() = default;

  virtual const char* id() const = 0;
  virtual const char* name() const = 0;
  virtual char shortcut() const = 0;
  virtual const char* hint() const { return name(); }

  virtual void on_press(CanvasEvent event) = 0;
  virtual void on_motion(CanvasEvent event) = 0;
  virtual void on_release(CanvasEvent event) = 0;
  virtual void on_cancel() = 0;
  virtual void on_double_click(CanvasEvent event) { (void)event; }
  // Enter: return true if the tool consumed the key (polygon / curve / text).
  virtual bool on_commit() { return false; }

  virtual Gtk::Widget* options_widget() { return nullptr; }
  // Chrome drawn above the canvas (text box border, handles, caret). Default
  // is a no-op so other tools are unchanged.
  virtual void draw_overlay(const Cairo::RefPtr<Cairo::Context>& cr, int origin_x, int origin_y,
                            double zoom) {
    (void)cr;
    (void)origin_x;
    (void)origin_y;
    (void)zoom;
  }
  // While a tool owns the keyboard (the text box). `text` is the typed UTF-8,
  // possibly empty for navigation keys. Return true if the key was consumed.
  virtual bool on_key(unsigned keyval, unsigned modifiers, const std::string& text) {
    (void)keyval;
    (void)modifiers;
    (void)text;
    return false;
  }
  // Document colors or other host state changed. Tools that preview from the
  // current foreground (the text box) refresh here; the default is a no-op.
  virtual void on_document_changed() {}
  virtual bool is_stroking() const { return false; }
  // When is_stroking() is true, CanvasView may composite tool_layer in place of
  // the active layer. Selection tools set is_stroking for pointer capture only
  // and must return false here so an empty tool_layer does not flash the
  // transparency checker over opaque canvas content (R-F03).
  virtual bool uses_tool_layer() const { return is_stroking(); }
  virtual bool captures_keys() const { return false; }
  // Shape tools: hollow vs filled toolbox buttons call this.
  virtual void set_shape_fill_mode(ShapeFillMode /*mode*/) {}

  void set_host(ToolHost* host) { host_ = host; }

protected:
  ToolHost* host_ = nullptr;

  Color stroke_color(unsigned button) const;
  bool ensure_editable();
};

}  // namespace lundukepaint

#endif
