// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_MAIN_WINDOW_HPP
#define LUNDUKEPAINT_APP_MAIN_WINDOW_HPP

#include "app/preferences.hpp"
#include "doc/document.hpp"
#include "doc/effect_preview.hpp"
#include "doc/workspace.hpp"
#include "io/image_io.hpp"
#include "tools/tool.hpp"
#include "ui/canvas_view.hpp"
#include "ui/colors_panel.hpp"
#include "ui/history_panel.hpp"
#include "ui/layers_panel.hpp"
#include "ui/status_bar.hpp"
#include "ui/pattern_strip.hpp"
#include "ui/toolbox.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <giomm/menumodel.h>
#include <giomm/simpleaction.h>
#include <glibmm/ustring.h>
#include <gtkmm/applicationwindow.h>
#include <gtkmm/box.h>
#include <gtkmm/frame.h>
#include <gtkmm/notebook.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/selectiondata.h>
#include <sigc++/connection.h>

namespace lundukepaint {

class LivePreviewDialog;

class MainWindow : public Gtk::ApplicationWindow, public ToolHost {
public:
  MainWindow();
  ~MainWindow() override;

  void reset_canvas();
  void new_document(int width, int height, Color background);
  // Plays the startup greeting once on a fresh empty canvas when enabled
  // (see LUNDUKEPAINT_HOWDY_INTRO; default OFF → blank canvas).
  void play_intro();
  void show_status(const Glib::ustring& message);
  void action_new();
  void action_open();
  bool open_path(const std::string& path, bool force_replace = false);
  void action_save();
  void action_save_as();
  bool confirm_close();
  Document& document() override { return workspace_.active(); }
  const Document& document() const { return workspace_.active(); }
  Document* document_ptr() { return workspace_.active_ptr(); }
  CanvasView& canvas() { return canvas_; }

  void set_active_tool(const std::string& id);
  Tool* active_tool() const { return active_tool_; }

  int stroke_size() const override { return stroke_size_; }
  void set_stroke_size(int size) override;
  bool brush_antialias() const override { return brush_aa_; }
  void set_brush_antialias(bool enabled) override;
  int fill_tolerance() const override { return fill_tolerance_; }
  void set_fill_tolerance(int tolerance) override;
  int pattern_index() const override { return pattern_index_; }
  void set_pattern_index(int index) override;
  const Pattern& active_pattern() const override;
  int brush_tip() const override { return brush_tip_; }
  void set_brush_tip(int index) override;
  int spray_radius() const override { return spray_radius_; }
  void set_spray_radius(int radius) override;
  void invalidate_canvas(Rect rect) override;
  void return_to_previous_tool() override;
  Color sample_canvas(int x, int y) const override;
  void show_status_hint(const char* message) override;
  bool canvas_to_screen(int canvas_x, int canvas_y, int& screen_x, int& screen_y) override;
  double canvas_zoom() const override { return canvas_.zoom(); }
  Gtk::Window* host_window() override { return this; }

  void update_chrome();
  void on_undo();
  void on_redo();
  void action_cut();
  void action_copy();
  void action_copy_merged();
  void action_paste();
  void action_delete();
  void action_duplicate();
  void action_select_all();
  void action_deselect();
  void action_invert_selection();
  void action_canvas_size();
  void action_scale();
  void action_crop();
  void action_autocrop();
  void action_rotate_90();
  void action_rotate_180();
  void action_rotate_ccw();
  void action_flip_h();
  void action_flip_v();
  void action_clear();
  void action_zoom_fit();
  void action_toggle_grid();
  void action_layer_new();
  void action_layer_duplicate();
  void action_layer_delete();
  void action_layer_raise();
  void action_layer_lower();
  void action_layer_merge_down();
  void action_layer_flatten();
  void action_layer_properties();
  void action_close_tab();
  void action_adjust_brightness();
  void action_adjust_invert();
  void action_adjust_grayscale();
  void action_adjust_hue();
  void action_adjust_posterize();
  void action_effect_blur();
  void action_effect_sharpen();
  void action_effect_emboss();
  void action_preferences();
  void action_shortcuts();
  void action_about();
  void action_print();
  void action_fullscreen();
  void action_revert();

private:
  void build_ui();
  void build_toolbar();
  Glib::RefPtr<Gio::MenuModel> load_menubar_model();
  void on_toggle_right_dock();
  void bind_document();
  void adopt_document(std::unique_ptr<Document> document, bool prefer_replace);
  void attach_active_document();
  void detach_document();
  void rebuild_tabs();
  void update_tab_labels();
  bool confirm_lose_document(Document& document);
  bool close_document_at(int index);
  std::string tab_title(const Document& document) const;
  void choose_color(bool background);
  bool on_key_press(GdkEventKey* event);
  bool on_key_release(GdkEventKey* event);
  bool focus_is_editable() const;
  void update_title();
  bool confirm_lose_changes();
  bool save_to_path(const std::string& path, ImageFormat format);
  std::string choose_open_path();
  bool choose_save_path(std::string& path, ImageFormat& format);
  bool layer_has_transparency() const;
  void composite_visible(std::vector<std::uint8_t>& dest) const;
  bool on_delete_event(GdkEventAny* event) override;
  void commit_buffer_change(const char* name, int new_w, int new_h, const std::uint8_t* rgba,
                            int stride);
  void commit_stack_transform(
      const char* name, int new_w, int new_h,
      const std::function<void(const Layer&, std::vector<std::uint8_t>&, int, int)>& xform);
  bool warn_size(int width, int height);
  void copy_selection_to_clipboard();
  void copy_merged_to_clipboard();
  bool paste_from_clipboard();
  void remember_recent(const std::string& path);
  void rebuild_recent_menu();
  void offer_recovery();
  bool on_recovery_tick();
  void on_drag_data_received(const Glib::RefPtr<Gdk::DragContext>& context, int x, int y,
                             const Gtk::SelectionData& data, guint info, guint time);
  void apply_layer_effect(const char* name,
                          const std::function<void(std::uint8_t*, int, int, int)>& fn);
  // Runs a modal adjustment dialog with optional live preview. build_effect
  // returns the effect for the dialog's current values, or nullptr for a no-op.
  bool run_adjust_dialog(LivePreviewDialog& dialog, const char* name,
                         const std::function<EffectPreview::EffectFn()>& build_effect);
  void apply_preferences();

  Gtk::Box root_{Gtk::ORIENTATION_VERTICAL};
  Gtk::Box toolbar_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::Notebook tab_bar_;
  Gtk::Box work_area_{Gtk::ORIENTATION_HORIZONTAL};
  Toolbox toolbox_;
  CanvasView canvas_;
  Gtk::Box center_column_{Gtk::ORIENTATION_VERTICAL};
  PatternStrip pattern_strip_;
  Gtk::Box right_sidebar_{Gtk::ORIENTATION_VERTICAL};
  Gtk::Frame layers_frame_{"Layers"};
  Gtk::Frame history_frame_{"History"};
  Gtk::Frame colors_frame_{"Colors"};
  ColorsPanel colors_panel_;
  LayersPanel layers_panel_;
  HistoryPanel history_panel_;
  StatusBar status_bar_;
  Workspace workspace_;
  Preferences prefs_;
  bool switching_tabs_{false};
  std::vector<std::unique_ptr<Tool>> tools_;
  Tool* active_tool_{nullptr};
  Tool* previous_tool_{nullptr};
  int stroke_size_{1};
  bool brush_aa_{true};
  int fill_tolerance_{0};
  int pattern_index_{0};
  int brush_tip_{3};
  int spray_radius_{16};
  int jpeg_quality_{90};
  bool intro_played_{false};
  Glib::RefPtr<Gio::SimpleAction> undo_action_;
  Glib::RefPtr<Gio::SimpleAction> redo_action_;
  Glib::RefPtr<Gio::SimpleAction> cut_action_;
  Glib::RefPtr<Gio::SimpleAction> copy_action_;
  Glib::RefPtr<Gio::SimpleAction> copy_merged_action_;
  Glib::RefPtr<Gio::SimpleAction> revert_action_;
  Glib::RefPtr<Gio::SimpleAction> delete_action_;
  Glib::RefPtr<Gio::SimpleAction> duplicate_action_;
  Glib::RefPtr<Gio::SimpleAction> deselect_action_;
  Glib::RefPtr<Gio::SimpleAction> invert_action_;
  Glib::RefPtr<Gio::SimpleAction> crop_action_;
  Glib::RefPtr<Gio::SimpleAction> layer_delete_action_;
  Glib::RefPtr<Gio::SimpleAction> layer_raise_action_;
  Glib::RefPtr<Gio::SimpleAction> layer_lower_action_;
  Glib::RefPtr<Gio::SimpleAction> layer_merge_action_;
  Glib::RefPtr<Gio::SimpleAction> layer_flatten_action_;
  Gtk::MenuItem* recent_item_{nullptr};
  sigc::connection recovery_timer_;
};

}  // namespace lundukepaint

#endif
