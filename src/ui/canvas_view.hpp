// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_CANVAS_VIEW_HPP
#define LUNDUKEPAINT_UI_CANVAS_VIEW_HPP

#include "raster/types.hpp"
#include "tools/tool.hpp"

#include <cairomm/path.h>
#include <cairomm/pattern.h>
#include <cairomm/surface.h>
#include <glibmm/refptr.h>
#include <gtkmm/drawingarea.h>
#include <gtkmm/layout.h>
#include <gtkmm/scrolledwindow.h>
#include <sigc++/connection.h>
#include <sigc++/signal.h>

#include <memory>
#include <vector>

namespace lundukepaint {

class Document;
class Layer;
class Tool;

class CanvasView : public Gtk::ScrolledWindow {
public:
  CanvasView();

  void set_document(Document* document);
  void set_tool(Tool* tool);
  void set_space_down(bool down);

  void reset_blank();
  void invalidate_rect(Rect rect);
  void invalidate_all();
  void refresh_size();
  // Re-center the document in the viewport after the pane size changes
  // (e.g. right dock shown/hidden).
  void recenter_in_viewport();

  // Give the drawing area keyboard focus so the single-letter tool shortcuts
  // reach MainWindow::on_key_press instead of the Size entry.
  void focus_canvas();

  // Startup greeting overlay (see ui/intro_howdy.hpp). Transient only: it never
  // touches the document, the layers or the dirty flag. After the 1s stroke
  // finishes the ink stays visible (still not dirty) until skip-during-play,
  // paint-over, or a new document.
  void start_intro();
  void cancel_intro();
  void skip_intro();
  bool intro_active() const { return intro_active_; }
  bool intro_visible() const { return intro_active_ || intro_held_; }

  double zoom() const { return zoom_; }
  void set_zoom(double zoom);
  void zoom_in();
  void zoom_out();
  void zoom_in_at(double widget_x, double widget_y);
  void zoom_out_at(double widget_x, double widget_y);
  void zoom_to(double zoom, double widget_x, double widget_y);
  void zoom_fit();

  bool grid_visible() const { return grid_visible_; }
  void set_grid_visible(bool visible);
  void set_checker_colors(Color light, Color dark);
  void set_grid_threshold(int percent);

  int canvas_width() const;
  int canvas_height() const;

  Color sample_pixel(int canvas_x, int canvas_y) const;

  void widget_to_canvas(double widget_x, double widget_y, double& canvas_x, double& canvas_y) const;
  bool canvas_to_screen(int canvas_x, int canvas_y, int& screen_x, int& screen_y) const;
  bool last_pointer(int& canvas_x, int& canvas_y) const;
  void viewport_center_canvas(int& canvas_x, int& canvas_y) const;
  void apply_zoom(double zoom);

  sigc::signal<void, double, double>& signal_pointer_moved() {
    return signal_pointer_moved_;
  }
  sigc::signal<void>& signal_pointer_left() { return signal_pointer_left_; }
  sigc::signal<void>& signal_view_changed() { return signal_view_changed_; }

protected:
  void on_size_allocate(Gtk::Allocation& allocation) override;

private:
  bool on_area_draw(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_area_motion(GdkEventMotion* event);
  bool on_area_leave(GdkEventCrossing* event);
  bool on_area_button_press(GdkEventButton* event);
  bool on_area_button_release(GdkEventButton* event);
  bool on_area_scroll(GdkEventScroll* event);

  void update_area_size();
  unsigned modifiers_from_state(guint state) const;
  CanvasEvent make_event(double widget_x, double widget_y, unsigned button, guint state) const;
  void begin_pan(double root_x, double root_y);
  void update_pan(double root_x, double root_y);
  void rebuild_checker();
  void rebuild_ants();
  double snapped_zoom(double zoom) const;
  void visible_center(double& x, double& y) const;
  int margin() const { return 24; }
  int content_pixel_width() const;
  int content_pixel_height() const;
  int origin_x() const;
  int origin_y() const;
  Color display_pixel(Color color, int x, int y) const;
  void draw_pixel_grid(const Cairo::RefPtr<Cairo::Context>& cr, int ox, int oy, int vis_x0,
                       int vis_y0, int vis_x1, int vis_y1);
  void draw_marching_ants(const Cairo::RefPtr<Cairo::Context>& cr, int ox, int oy);
  void ensure_ants_timer();
  void invalidate_ants();
  void draw_intro(const Cairo::RefPtr<Cairo::Context>& cr);
  bool on_intro_tick();

  Gtk::Layout layout_;
  Gtk::DrawingArea area_;
  Document* document_{nullptr};
  Tool* tool_{nullptr};
  double zoom_{1.0};
  bool grid_visible_{true};
  Color checker_light_{209, 209, 209, 255};
  Color checker_dark_{158, 158, 158, 255};
  int grid_threshold_{400};
  int ants_phase_{0};
  sigc::connection ants_timer_;
  std::unique_ptr<Cairo::Path> ants_path_;
  std::vector<Rect> ants_halos_;
  unsigned ants_generation_{0};
  int area_move_x_{0};
  int area_move_y_{0};
  Cairo::RefPtr<Cairo::ImageSurface> blit_surface_;
  int blit_w_{0};
  int blit_h_{0};
  Cairo::RefPtr<Cairo::SurfacePattern> checker_pattern_;
  bool space_down_{false};
  bool panning_{false};
  double pan_start_x_{0};
  double pan_start_y_{0};
  double pan_hadj_{0};
  double pan_vadj_{0};
  bool updating_size_{false};
  bool intro_active_{false};
  bool intro_held_{false};
  gint64 intro_start_us_{0};
  sigc::connection intro_timer_;

  sigc::signal<void, double, double> signal_pointer_moved_;
  sigc::signal<void> signal_pointer_left_;
  sigc::signal<void> signal_view_changed_;
  bool has_pointer_{false};
  double last_cx_{0};
  double last_cy_{0};
};

}  // namespace lundukepaint

#endif
