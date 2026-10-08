// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/main_window.hpp"

#include "app/actions.hpp"
#include "app/live_edit.hpp"
#include "raster/brush_tip.hpp"
#include "app/shortcut_dispatch.hpp"
#include "app/shortcut_help.hpp"
#include "doc/commands_image.hpp"
#include "doc/commands_layers.hpp"
#include "doc/commands_pixels.hpp"
#include "doc/effect_preview.hpp"
#include "doc/selection.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"
#include "io/crash_recovery.hpp"
#include "raster/brush_tip.hpp"
#include "raster/effects.hpp"
#include "raster/transform.hpp"
#include "ui/dialogs_adjust.hpp"
#include "ui/dialogs_image.hpp"
#include "ui/dialogs_new.hpp"
#include "ui/dialogs_prefs.hpp"
#include "ui/intro_howdy.hpp"
#include "ui/toolbox_catalog.hpp"
#include "tools/tools.hpp"

#include <unistd.h>

#include <mutex>
#include <thread>

#include <glibmm/error.h>
#include <giomm/menu.h>
#include <gtkmm/builder.h>
#include <glibmm/miscutils.h>
#include <glibmm/main.h>
#include <gtk/gtk.h>
#include <gdkmm/pixbuf.h>
#include <gtkmm/button.h>
#include <gtkmm/label.h>
#include <gtkmm/clipboard.h>
#include <gtkmm/colorchooserdialog.h>
#include <gtkmm/filechooserdialog.h>
#include <gtkmm/filefilter.h>
#include <gtkmm/image.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/menubar.h>
#include <gtkmm/separator.h>
#include <gtkmm/aboutdialog.h>
#include <gtkmm/printoperation.h>
#include <gtkmm/grid.h>
#include <gtkmm/spinbutton.h>
#include <giomm/file.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/targetentry.h>
#include <gtkmm/separatormenuitem.h>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <vector>
#include <stdexcept>

namespace lundukepaint {
namespace {

Gtk::Button* toolbar_button(const char* icon, const char* tooltip, const char* action) {
  auto* button = Gtk::manage(new Gtk::Button());
  button->set_image_from_icon_name(icon, Gtk::ICON_SIZE_SMALL_TOOLBAR);
  button->set_tooltip_text(tooltip);
  gtk_actionable_set_action_name(GTK_ACTIONABLE(button->gobj()), action);
  button->set_can_focus(false);
  return button;
}

}  // namespace

struct MainWindow::RecoverySlot {
  std::mutex mu;
  MainWindow* window = nullptr;
  bool busy = false;
  bool pending = false;
};

struct RecoveryIdle {
  std::shared_ptr<MainWindow::RecoverySlot> slot;
};

MainWindow::MainWindow() {
  recovery_slot_ = std::make_shared<RecoverySlot>();
  recovery_slot_->window = this;
  last_edit_us_ = g_get_monotonic_time();
  prefs_.load();
  auto startup = Document::create(prefs_.default_width, prefs_.default_height, Color::white());
  startup->history().set_depth(prefs_.undo_limit);
  workspace_.add(std::move(startup));
  set_title(Glib::ustring("Untitled — ") + actions::kProductName);
  set_default_size(1100, 720);
  // Reinforce default icon for WMs that ignore gtk_window_set_default_icon_name.
  set_icon_name(actions::kAppId);
  // Traditional WM decorations: do not call set_titlebar() / GtkHeaderBar.

  add_action(actions::kToggleRightDock, sigc::mem_fun(*this, &MainWindow::on_toggle_right_dock));
  undo_action_ = add_action(actions::kUndo, sigc::mem_fun(*this, &MainWindow::on_undo));
  redo_action_ = add_action(actions::kRedo, sigc::mem_fun(*this, &MainWindow::on_redo));
  add_action(actions::kZoomIn, [this]() { canvas_.zoom_in(); });
  add_action(actions::kZoomOut, [this]() { canvas_.zoom_out(); });
  add_action(actions::kZoom100, [this]() { canvas_.set_zoom(1.0); });
  add_action(actions::kZoomFit, sigc::mem_fun(*this, &MainWindow::action_zoom_fit));
  add_action(actions::kToggleGrid, sigc::mem_fun(*this, &MainWindow::action_toggle_grid));
  cut_action_ = add_action(actions::kCut, sigc::mem_fun(*this, &MainWindow::action_cut));
  copy_action_ = add_action(actions::kCopy, sigc::mem_fun(*this, &MainWindow::action_copy));
  copy_merged_action_ = add_action(actions::kCopyMerged, sigc::mem_fun(*this, &MainWindow::action_copy_merged));
  add_action(actions::kPaste, sigc::mem_fun(*this, &MainWindow::action_paste));
  add_action(actions::kPasteIntoNew, sigc::mem_fun(*this, &MainWindow::action_paste_into_new));
  delete_action_ = add_action(actions::kDelete, sigc::mem_fun(*this, &MainWindow::action_delete));
  duplicate_action_ = add_action(actions::kDuplicate, sigc::mem_fun(*this, &MainWindow::action_duplicate));
  add_action(actions::kSelectAll, sigc::mem_fun(*this, &MainWindow::action_select_all));
  deselect_action_ = add_action(actions::kDeselect, sigc::mem_fun(*this, &MainWindow::action_deselect));
  invert_action_ = add_action(actions::kInvertSelection,
                             sigc::mem_fun(*this, &MainWindow::action_invert_selection));
  add_action(actions::kCanvasSize, sigc::mem_fun(*this, &MainWindow::action_canvas_size));
  add_action(actions::kScale, sigc::mem_fun(*this, &MainWindow::action_scale));
  crop_action_ = add_action(actions::kCrop, sigc::mem_fun(*this, &MainWindow::action_crop));
  add_action(actions::kAutocrop, sigc::mem_fun(*this, &MainWindow::action_autocrop));
  add_action(actions::kRotate90, sigc::mem_fun(*this, &MainWindow::action_rotate_90));
  add_action(actions::kRotate180, sigc::mem_fun(*this, &MainWindow::action_rotate_180));
  add_action(actions::kRotateCcw, sigc::mem_fun(*this, &MainWindow::action_rotate_ccw));
  add_action(actions::kFlipH, sigc::mem_fun(*this, &MainWindow::action_flip_h));
  add_action(actions::kFlipV, sigc::mem_fun(*this, &MainWindow::action_flip_v));
  add_action(actions::kClear, sigc::mem_fun(*this, &MainWindow::action_clear));
  add_action(actions::kPrint, sigc::mem_fun(*this, &MainWindow::action_print));
  add_action(actions::kLayerNew, sigc::mem_fun(*this, &MainWindow::action_layer_new));
  add_action(actions::kLayerDuplicate, sigc::mem_fun(*this, &MainWindow::action_layer_duplicate));
  layer_delete_action_ = add_action(actions::kLayerDelete,
                                    sigc::mem_fun(*this, &MainWindow::action_layer_delete));
  layer_raise_action_ = add_action(actions::kLayerRaise,
                                   sigc::mem_fun(*this, &MainWindow::action_layer_raise));
  layer_lower_action_ = add_action(actions::kLayerLower,
                                   sigc::mem_fun(*this, &MainWindow::action_layer_lower));
  layer_merge_action_ = add_action(actions::kLayerMergeDown,
                                   sigc::mem_fun(*this, &MainWindow::action_layer_merge_down));
  layer_flatten_action_ = add_action(actions::kLayerFlatten,
                                     sigc::mem_fun(*this, &MainWindow::action_layer_flatten));
  add_action(actions::kLayerProperties, sigc::mem_fun(*this, &MainWindow::action_layer_properties));
  add_action(actions::kCloseTab, sigc::mem_fun(*this, &MainWindow::action_close_tab));
  add_action(actions::kAdjustBrightness, sigc::mem_fun(*this, &MainWindow::action_adjust_brightness));
  add_action(actions::kAdjustInvert, sigc::mem_fun(*this, &MainWindow::action_adjust_invert));
  add_action(actions::kAdjustGrayscale, sigc::mem_fun(*this, &MainWindow::action_adjust_grayscale));
  add_action(actions::kAdjustHue, sigc::mem_fun(*this, &MainWindow::action_adjust_hue));
  add_action(actions::kAdjustPosterize, sigc::mem_fun(*this, &MainWindow::action_adjust_posterize));
  add_action(actions::kEffectBlur, sigc::mem_fun(*this, &MainWindow::action_effect_blur));
  add_action(actions::kEffectSharpen, sigc::mem_fun(*this, &MainWindow::action_effect_sharpen));
  add_action(actions::kEffectEmboss, sigc::mem_fun(*this, &MainWindow::action_effect_emboss));
  add_action(actions::kPreferences, sigc::mem_fun(*this, &MainWindow::action_preferences));
  add_action(actions::kShortcuts, sigc::mem_fun(*this, &MainWindow::action_shortcuts));
  add_action(actions::kAbout, sigc::mem_fun(*this, &MainWindow::action_about));
  add_action(actions::kFullscreen, sigc::mem_fun(*this, &MainWindow::action_fullscreen));
  revert_action_ = add_action(actions::kRevert, sigc::mem_fun(*this, &MainWindow::action_revert));
  add_action("recent-none", []() {});
  add_action(actions::kClearRecent, [this]() {
    prefs_.recent_files.clear();
    prefs_.save();
    rebuild_recent_menu();
  });

  // MacPaint toolbox set (+ keep ellipse-select/wand/picker/polyline/curve for shortcuts).
  tools_.emplace_back(create_lasso_tool());
  tools_.emplace_back(create_rect_select_tool());
  tools_.emplace_back(create_hand_tool());
  tools_.emplace_back(create_text_tool());
  tools_.emplace_back(create_fill_tool());
  tools_.emplace_back(create_spray_tool());
  tools_.emplace_back(create_brush_tool());
  tools_.emplace_back(create_pencil_tool());
  tools_.emplace_back(create_line_tool());
  tools_.emplace_back(create_eraser_tool());
  tools_.emplace_back(create_rectangle_tool());
  tools_.emplace_back(create_rectangle_fill_tool());
  tools_.emplace_back(create_rounded_rect_tool());
  tools_.emplace_back(create_rounded_rect_fill_tool());
  tools_.emplace_back(create_ellipse_tool());
  tools_.emplace_back(create_ellipse_fill_tool());
  tools_.emplace_back(create_freeform_tool());
  tools_.emplace_back(create_freeform_fill_tool());
  tools_.emplace_back(create_polygon_tool());
  tools_.emplace_back(create_polygon_fill_tool());
  tools_.emplace_back(create_ellipse_select_tool());
  tools_.emplace_back(create_magic_wand_tool());
  tools_.emplace_back(create_picker_tool());
  tools_.emplace_back(create_polyline_tool());
  tools_.emplace_back(create_curve_tool());
  for (auto& tool : tools_) {
    tool->set_host(this);
  }
  active_tool_ = tools_.front().get();

  add_events(Gdk::KEY_PRESS_MASK | Gdk::KEY_RELEASE_MASK);
  signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_key_press), false);
  signal_key_release_event().connect(sigc::mem_fun(*this, &MainWindow::on_key_release), false);

  build_ui();
  apply_preferences();
  bind_document();
  set_active_tool("pencil");
  show_all();
  // Keep initial focus on the canvas so single-letter tool shortcuts work
  // (editable widgets in docks must not steal first focus).
  canvas_.focus_canvas();
  rebuild_tabs();
  update_chrome();
  rebuild_recent_menu();
  std::vector<Gtk::TargetEntry> targets;
  targets.emplace_back("text/uri-list");
  drag_dest_set(targets, Gtk::DEST_DEFAULT_ALL, Gdk::ACTION_COPY);
  signal_drag_data_received().connect(sigc::mem_fun(*this, &MainWindow::on_drag_data_received));
  recovery_timer_ = Glib::signal_timeout().connect_seconds(
      sigc::mem_fun(*this, &MainWindow::on_recovery_tick), 30);
  Glib::signal_idle().connect([this]() {
    offer_recovery();
    return false;
  });
}

MainWindow::~MainWindow() {
  toolbox_.set_tool_options(nullptr);
  recovery_timer_.disconnect();
  if (recovery_slot_) {
    std::lock_guard<std::mutex> lock(recovery_slot_->mu);
    recovery_slot_->window = nullptr;
  }
}

gboolean MainWindow::recovery_idle_cb(gpointer data) {
  std::unique_ptr<RecoveryIdle> args(static_cast<RecoveryIdle*>(data));
  MainWindow* window = nullptr;
  bool again = false;
  {
    std::lock_guard<std::mutex> lock(args->slot->mu);
    window = args->slot->window;
    args->slot->busy = false;
    again = window != nullptr && args->slot->pending;
    if (again) {
      args->slot->pending = false;
    }
  }
  if (again && window != nullptr) {
    window->start_recovery_save();
  }
  return G_SOURCE_REMOVE;
}

void MainWindow::build_ui() {
  auto model = load_menubar_model();
  auto* menubar = Gtk::make_managed<Gtk::MenuBar>(model);
  auto mchildren = menubar->get_children();
  if (!mchildren.empty()) {
    if (auto* file_item = dynamic_cast<Gtk::MenuItem*>(mchildren[0])) {
      if (auto* file_menu = file_item->get_submenu()) {
        for (auto* child : file_menu->get_children()) {
          if (auto* item = dynamic_cast<Gtk::MenuItem*>(child)) {
            if (item->get_label().find("Recent") != Glib::ustring::npos) {
              recent_item_ = item;
              break;
            }
          }
        }
      }
    }
  }

  build_toolbar();

  canvas_.set_hexpand(true);
  canvas_.set_vexpand(true);

  // MacPaint order, then the tools that used to be keyboard-only. Icons are the
  // app's own symbolic SVGs.
  int catalog_count = 0;
  const ToolboxTool* catalog = toolbox_tools(catalog_count);
  for (int i = 0; i < catalog_count; ++i) {
    toolbox_.add_tool_button(catalog[i].id, catalog[i].tooltip, catalog[i].icon);
  }
  toolbox_.on_tool_chosen = [this](const std::string& id) {
    set_active_tool(id);
    canvas_.focus_canvas();
  };
  toolbox_.on_line_width_chosen = [this](int width) { set_stroke_size(width); };
  toolbox_.set_line_width(stroke_size_);
  toolbox_.on_brush_tip_chosen = [this](int index) { set_brush_tip(index); };
  toolbox_.set_brush_tip(brush_tip_);
  toolbox_.on_spray_radius_chosen = [this](int radius) { set_spray_radius(radius); };
  toolbox_.set_spray_radius(spray_radius_);
  toolbox_.on_swap_colors = [this]() { document().swap_colors(); };
  toolbox_.on_reset_colors = [this]() { document().reset_colors(); };
  toolbox_.on_edit_color = [this](bool background) { choose_color(background); };

  pattern_strip_.on_pattern_chosen = [this](int index) { set_pattern_index(index); };
  colors_panel_.on_swatch = [this](Color color, bool background) {
    if (background) {
      document().set_background(color);
    } else {
      document().set_foreground(color);
    }
  };

  constexpr int kRightDockWidth = 240;
  layers_frame_.set_shadow_type(Gtk::SHADOW_IN);
  history_frame_.set_shadow_type(Gtk::SHADOW_IN);
  colors_frame_.set_shadow_type(Gtk::SHADOW_IN);
  layers_frame_.add(layers_panel_);
  history_frame_.add(history_panel_);
  colors_frame_.add(colors_panel_);
  layers_panel_.set_hexpand(true);
  layers_panel_.set_vexpand(true);
  history_panel_.set_hexpand(true);
  history_panel_.set_vexpand(true);
  colors_panel_.set_hexpand(true);
  colors_panel_.set_valign(Gtk::ALIGN_END);

  right_sidebar_.set_size_request(kRightDockWidth, -1);
  layers_frame_.set_size_request(kRightDockWidth, -1);
  history_frame_.set_size_request(kRightDockWidth, 140);
  colors_frame_.set_size_request(kRightDockWidth, -1);

  right_sidebar_.set_spacing(2);
  right_sidebar_.set_hexpand(false);
  right_sidebar_.set_halign(Gtk::ALIGN_FILL);
  // Top Layers (grows) / middle History / bottom Colors (not a notebook).
  right_sidebar_.pack_start(layers_frame_, Gtk::PACK_EXPAND_WIDGET);
  right_sidebar_.pack_start(history_frame_, Gtk::PACK_SHRINK);
  right_sidebar_.pack_start(colors_frame_, Gtk::PACK_SHRINK);

  center_column_.set_spacing(0);
  center_column_.set_hexpand(true);
  center_column_.set_vexpand(true);
  canvas_.set_hexpand(true);
  canvas_.set_vexpand(true);
  pattern_strip_.set_hexpand(true);
  // Pattern strip spans under the canvas only (not under the right rail).
  center_column_.pack_start(canvas_, Gtk::PACK_EXPAND_WIDGET);
  center_column_.pack_start(pattern_strip_, Gtk::PACK_SHRINK);

  auto* left_sep = Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_VERTICAL);

  work_area_.pack_start(toolbox_, Gtk::PACK_SHRINK);
  work_area_.pack_start(*left_sep, Gtk::PACK_SHRINK);
  work_area_.pack_start(center_column_, Gtk::PACK_EXPAND_WIDGET);
  work_area_.pack_start(right_sidebar_, Gtk::PACK_SHRINK);
  work_area_.set_hexpand(true);
  work_area_.set_vexpand(true);

  tab_bar_.set_scrollable(true);
  tab_bar_.set_show_border(false);
  tab_bar_.set_no_show_all(true);
  tab_bar_.signal_switch_page().connect([this](Gtk::Widget*, guint page) {
    if (switching_tabs_) {
      return;
    }
    if (static_cast<int>(page) == workspace_.active_index()) {
      return;
    }
    if (!preserve_live_edits_for_tab_switch()) {
      switching_tabs_ = true;
      tab_bar_.set_current_page(workspace_.active_index());
      switching_tabs_ = false;
      return;
    }
    workspace_.set_active(static_cast<int>(page));
    attach_active_document();
  });

  root_.pack_start(*menubar, Gtk::PACK_SHRINK);
  root_.pack_start(toolbar_, Gtk::PACK_SHRINK);
  root_.pack_start(tab_bar_, Gtk::PACK_SHRINK);
  root_.pack_start(work_area_, Gtk::PACK_EXPAND_WIDGET);
  root_.pack_start(status_bar_, Gtk::PACK_SHRINK);

  canvas_.signal_pointer_moved().connect(
      sigc::mem_fun(status_bar_, &StatusBar::show_coordinates));
  canvas_.signal_pointer_left().connect(
      sigc::mem_fun(status_bar_, &StatusBar::clear_coordinates));
  canvas_.signal_view_changed().connect([this]() {
    if (document_ptr() != nullptr) {
      document().set_view_zoom(canvas_.zoom());
    }
    update_chrome();
  });

  add(root_);
}

void MainWindow::build_toolbar() {
  toolbar_.set_spacing(4);
  toolbar_.set_border_width(4);
  toolbar_.get_style_context()->add_class("toolbar");
  toolbar_.set_size_request(-1, 36);

  toolbar_.pack_start(*toolbar_button("document-new", "New", "app.new"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("document-open", "Open", "app.open"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("document-save", "Save", "app.save"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("edit-cut", "Cut", "win.cut"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("edit-copy", "Copy", "win.copy"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("edit-paste", "Paste", "win.paste"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("edit-undo", "Undo", "win.undo"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("edit-redo", "Redo", "win.redo"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("zoom-out", "Zoom out", "win.zoom-out"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("zoom-original", "100%", "win.zoom-100"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("zoom-in", "Zoom in", "win.zoom-in"), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL)), Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_button("view-sidebar-end-symbolic", "Layers, history, and colors (F12)",
                                     "win.toggle-right-dock"),
                      Gtk::PACK_SHRINK);
}

Glib::RefPtr<Gio::MenuModel> MainWindow::load_menubar_model() {
  try {
    auto builder = Gtk::Builder::create_from_resource(
        "/org/lunduke/LundukePaint/ui/menus.xml");
    auto object = builder->get_object("menubar");
    auto menu = Glib::RefPtr<Gio::Menu>::cast_dynamic(object);
    if (!menu) {
      throw std::runtime_error("menus.xml is missing the 'menubar' object");
    }
    return menu;
  } catch (const Glib::Error& error) {
    std::cerr << "Failed to load menus.xml: " << error.what() << '\n';
    throw;
  }
}

void MainWindow::bind_document() {
  attach_active_document();
}

void MainWindow::detach_document() {
  // The panels keep a raw Document*. Clear them before the workspace frees the
  // document they point at, otherwise the refresh that canvas_.set_document()
  // triggers reads freed memory (this crashed every File > Open).
  canvas_.cancel_intro();
  layers_panel_.set_document(nullptr);
  history_panel_.set_document(nullptr);
  canvas_.set_document(nullptr);
}

void MainWindow::attach_active_document() {
  // Panels first: canvas_.set_document() emits view-changed, which runs
  // update_chrome(), which refreshes both panels.
  layers_panel_.set_document(document_ptr());
  history_panel_.set_document(document_ptr());
  canvas_.set_document(document_ptr());
  canvas_.apply_zoom(document().view_zoom());
  canvas_.set_tool(active_tool_);
  history_panel_.on_jump = [this](int index) { document().jump_history(index); };
  document().set_on_blocked([this](const char* message) { report_blocked(message); });
  document().set_on_disrupt([this](const char* action) { return disrupt_allowed(action); });
  document().set_on_changed([this]() {
    last_edit_us_ = g_get_monotonic_time();
    update_chrome();
  });
  document().set_on_selection([this]() { update_selection_status(); });
  document().set_on_invalidated([this](Rect rect) { canvas_.invalidate_rect(rect); });
  pattern_strip_.set_colors(document().foreground(), document().background());
  update_chrome();
}

void MainWindow::reset_canvas() {
  new_document(kDefaultWidth, kDefaultHeight, Color::white());
}

void MainWindow::new_document(int width, int height, Color background) {
  adopt_document(Document::create(width, height, background), false);
}

bool MainWindow::adopt_document(std::unique_ptr<Document> document, bool prefer_replace, LivePath path) {
  if (!document) {
    return false;
  }
  document->history().set_depth(prefs_.undo_limit);
  if (document_ptr() != nullptr &&
      settle_tool_live(active_tool_, document_ptr(), path) == SettleResult::Blocked) {
    return false;
  }
  detach_document();
  if (prefer_replace && workspace_.count() == 1 && workspace_.is_placeholder(0)) {
    workspace_.replace_active(std::move(document));
  } else {
    workspace_.add(std::move(document));
  }
  attach_active_document();
  rebuild_tabs();
  return true;
}

void MainWindow::play_intro() {
#if LUNDUKEPAINT_HOWDY_INTRO
  if (intro_played_) {
    return;
  }
  intro_played_ = true;
  const Document* doc = workspace_.active_ptr();
  if (doc == nullptr) {
    return;
  }
  // Only a genuinely untouched, unsaved, empty canvas gets the greeting.
  if (doc->dirty() || !doc->path().empty() || doc->history().can_undo()) {
    return;
  }
  canvas_.start_intro();
#else
  // Howdy intro shelved (LUNDUKEPAINT_HOWDY_INTRO=0): fresh launch stays blank.
  (void)intro_played_;
#endif
}

void MainWindow::show_status(const Glib::ustring& message) {
  status_bar_.show_message(message);
}

void MainWindow::report_blocked(const char* message) {
  if (message == nullptr || message[0] == '\0' || blocked_dialog_) {
    return;
  }
  blocked_dialog_ = true;
  Gtk::MessageDialog err(*this, message, false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
  err.run();
  blocked_dialog_ = false;
}

bool MainWindow::preserve_live_edits_for_tab_switch() {
  if (active_tool_ == nullptr || document_ptr() == nullptr) {
    return true;
  }
  return settle_tool_live(active_tool_, document_ptr(), LivePath::TabSwitch) != SettleResult::Blocked;
}

bool MainWindow::disrupt_allowed(const char* action) {
  LivePath path = LivePath::LayerChange;
  if (action != nullptr) {
    const std::string name(action);
    if (name == "undo") {
      path = LivePath::Undo;
    } else if (name == "redo") {
      path = LivePath::Redo;
    } else if (name == "history-jump") {
      path = LivePath::HistoryJump;
    } else if (name == "layer-add") {
      path = LivePath::LayerAdd;
    } else if (name == "layer-delete") {
      path = LivePath::LayerDelete;
    } else if (name == "layer-move") {
      path = LivePath::LayerMove;
    } else if (name == "layer-lock") {
      path = LivePath::LayerLock;
    } else if (name == "paste") {
      path = LivePath::Paste;
    }
  }
  if (settle_tool_live(active_tool_, document_ptr(), path) == SettleResult::Blocked) {
    if (path == LivePath::Undo || path == LivePath::Redo || path == LivePath::HistoryJump) {
      show_status("Finish or cancel the in-progress edit first");
    }
    return false;
  }
  return true;
}

bool MainWindow::other_documents_dirty(const Document* except) const {
  for (int i = 0; i < workspace_.count(); ++i) {
    if (&workspace_.at(i) == except) {
      continue;
    }
    if (workspace_.at(i).dirty()) {
      return true;
    }
  }
  return false;
}

void MainWindow::clear_document_recovery(const Document& document) {
  crash_recovery::clear_file(crash_recovery::autosave_path_for(document.recovery_id()));
  if (!other_documents_dirty(&document)) {
    crash_recovery::clear();
  }
}

void MainWindow::set_active_tool(const std::string& id) {
  Tool* found = nullptr;
  for (auto& tool : tools_) {
    if (id == tool->id()) {
      found = tool.get();
      break;
    }
  }
  if (found == nullptr || found == active_tool_) {
    if (found != nullptr) {
      toolbox_.set_active_tool(id);
      toolbox_.set_tool_options(found->options_widget());
    }
    return;
  }
  if (active_tool_ != nullptr) {
    if (settle_tool_live(active_tool_, document_ptr(), LivePath::ToolChange) == SettleResult::Blocked) {
      toolbox_.set_active_tool(active_tool_->id());
      toolbox_.set_tool_options(active_tool_->options_widget());
      return;
    }
    const bool floating = document_ptr() != nullptr && document().selection().floating();
    if (!floating && active_tool_->is_stroking() && !active_tool_->uses_tool_layer()) {
      active_tool_->on_cancel();
    } else {
      active_tool_->release_pointer();
    }
    previous_tool_ = active_tool_;
  }
  active_tool_ = found;
  canvas_.set_tool(active_tool_);
  toolbox_.set_active_tool(id);
  active_tool_->sync_options_from_document();
  toolbox_.set_tool_options(active_tool_->options_widget());
  status_bar_.set_hint(active_tool_->hint());
}

void MainWindow::set_stroke_size(int size) {
  if (size < 1) {
    size = 1;
  }
  stroke_size_ = size;
  toolbox_.set_line_width(stroke_size_);
}

void MainWindow::set_pattern_index(int index) {
  pattern_index_ = clamp_pattern_index(index);
  pattern_strip_.set_pattern_index(pattern_index_);
}

const Pattern& MainWindow::active_pattern() const {
  return pattern_at(pattern_index_);
}

void MainWindow::set_brush_tip(int index) {
  brush_tip_ = clamp_brush_tip_index(index);
  toolbox_.set_brush_tip(brush_tip_);
  // The tip picker is its own control. It must not move the line-width mark
  // onto a size that stack cannot show.
  const BrushTip tip = brush_tip_at(brush_tip_);
  set_stroke_size(stroke_after_selecting_brush_tip(stroke_size_, std::max(1, tip.size)));
}

void MainWindow::set_spray_radius(int radius) {
  if (radius < 1) {
    radius = 1;
  }
  spray_radius_ = radius;
  toolbox_.set_spray_radius(spray_radius_);
}

void MainWindow::set_brush_antialias(bool enabled) {
  brush_aa_ = enabled;
}

void MainWindow::set_fill_tolerance(int tolerance) {
  if (tolerance < 0) {
    tolerance = 0;
  }
  fill_tolerance_ = tolerance;
}

void MainWindow::invalidate_canvas(Rect rect) {
  canvas_.invalidate_rect(rect);
}

void MainWindow::return_to_previous_tool() {
  if (previous_tool_ != nullptr) {
    set_active_tool(previous_tool_->id());
  }
}

Color MainWindow::sample_canvas(int x, int y) const {
  return canvas_.sample_pixel(x, y);
}

void MainWindow::show_status_hint(const char* message) {
  if (message != nullptr) {
    show_status(message);
  }
}

bool MainWindow::canvas_to_screen(int canvas_x, int canvas_y, int& screen_x, int& screen_y) {
  return canvas_.canvas_to_screen(canvas_x, canvas_y, screen_x, screen_y);
}

void MainWindow::on_undo() {
  document().undo();
}

void MainWindow::on_redo() {
  document().redo();
}

void MainWindow::update_selection_status() {
  if (document_ptr() == nullptr) {
    return;
  }
  const Selection& sel = document().selection();
  const bool has_sel = !sel.empty();
  if (cut_action_) {
    cut_action_->set_enabled(has_sel);
  }
  if (copy_action_) {
    copy_action_->set_enabled(has_sel);
  }
  if (delete_action_) {
    delete_action_->set_enabled(has_sel);
  }
  if (duplicate_action_) {
    duplicate_action_->set_enabled(has_sel && !sel.inverted());
  }
  if (deselect_action_) {
    deselect_action_->set_enabled(has_sel);
  }
  if (crop_action_) {
    crop_action_->set_enabled(has_sel && !sel.inverted());
  }
  if (has_sel) {
    const Rect b = sel.bounds();
    status_bar_.set_selection_size(b.w, b.h, true);
  } else {
    status_bar_.set_selection_size(0, 0, false);
  }
}

void MainWindow::update_chrome() {
  update_title();
  undo_action_->set_enabled(document().history().can_undo());
  redo_action_->set_enabled(document().history().can_redo());
  const Selection& sel = document().selection();
  const bool has_sel = !sel.empty();
  cut_action_->set_enabled(has_sel);
  copy_action_->set_enabled(has_sel);
  if (copy_merged_action_) {
    copy_merged_action_->set_enabled(true);
  }
  if (revert_action_) {
    revert_action_->set_enabled(!document().path().empty());
  }
  delete_action_->set_enabled(has_sel);
  duplicate_action_->set_enabled(has_sel && !sel.inverted());
  deselect_action_->set_enabled(has_sel);
  invert_action_->set_enabled(true);
  crop_action_->set_enabled(has_sel && !sel.inverted());
  if (has_sel) {
    const Rect b = sel.bounds();
    status_bar_.set_selection_size(b.w, b.h, true);
  } else {
    status_bar_.set_selection_size(0, 0, false);
  }
  status_bar_.set_canvas_size(document().width(), document().height());
  status_bar_.set_zoom(canvas_.zoom());
  status_bar_.set_modified(document().dirty());
  if (active_tool_ != nullptr) {
    status_bar_.set_hint(active_tool_->hint());
    active_tool_->on_document_changed();
  }
  pattern_strip_.set_colors(document().foreground(), document().background());
  colors_panel_.set_colors(document().foreground(), document().background());
  toolbox_.set_colors(document().foreground(), document().background());
  canvas_.refresh_size();
  layers_panel_.refresh();
  history_panel_.refresh();
  const int nlayers = document().layers().count();
  const int active = document().layers().active_index();
  layer_delete_action_->set_enabled(nlayers > 1);
  layer_raise_action_->set_enabled(active + 1 < nlayers);
  layer_lower_action_->set_enabled(active > 0);
  layer_merge_action_->set_enabled(active > 0);
  layer_flatten_action_->set_enabled(nlayers > 1);
  update_tab_labels();
}

void MainWindow::update_title() {
  Glib::ustring name = "Untitled";
  if (!document().path().empty()) {
    name = Glib::path_get_basename(document().path());
  }
  if (document().dirty()) {
    name += "*";
  }
  set_title(name + " — " + actions::kProductName);
}

void MainWindow::choose_color(bool background) {
  Gtk::ColorChooserDialog dialog(background ? "Background color" : "Foreground color");
  dialog.set_transient_for(*this);
  dialog.set_use_alpha(true);
  const Color current = background ? document().background() : document().foreground();
  Gdk::RGBA rgba;
  rgba.set_rgba(current.r / 255.0, current.g / 255.0, current.b / 255.0, current.a / 255.0);
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
  if (background) {
    document().set_background(color);
  } else {
    document().set_foreground(color);
  }
}

bool MainWindow::focus_is_editable() const {
  auto* focus = get_focus();
  if (focus == nullptr) {
    return false;
  }
  return GTK_IS_EDITABLE(focus->gobj());
}

bool MainWindow::on_key_press(GdkEventKey* event) {
  if (event == nullptr) {
    return false;
  }
  canvas_.skip_intro();  // skip mid-animation; a finished howdy stays
  if (event->keyval == GDK_KEY_Escape && active_tool_ != nullptr && active_tool_->captures_keys()) {
    active_tool_->on_cancel();
    return true;
  }
  const bool texting =
      active_tool_ != nullptr && active_tool_->captures_keys() && !focus_is_editable();
  auto mods_of = [](guint state) {
    unsigned mods = 0;
    if ((state & GDK_SHIFT_MASK) != 0) {
      mods |= Modifier::Shift;
    }
    if ((state & GDK_CONTROL_MASK) != 0) {
      mods |= Modifier::Ctrl;
    }
    if ((state & GDK_MOD1_MASK) != 0) {
      mods |= Modifier::Alt;
    }
    return mods;
  };
  if (event->keyval == GDK_KEY_space) {
    if (texting) {
      const std::string typed = event->string != nullptr ? event->string : " ";
      return active_tool_->on_key(event->keyval, mods_of(event->state), typed);
    }
    canvas_.set_space_down(true);
    return false;
  }
  if (focus_is_editable()) {
    return false;
  }
  if (event->keyval == GDK_KEY_Escape) {
    const bool busy =
        active_tool_ != nullptr && (active_tool_->is_stroking() || active_tool_->captures_keys());
    const bool floating = document_ptr() != nullptr && document().selection().floating();
    switch (escape_target(busy, floating)) {
      case EscapeTarget::ToolCancel:
        active_tool_->on_cancel();
        return true;
      case EscapeTarget::FloatCancel:
        document().cancel_floating();
        return true;
      case EscapeTarget::Deselect:
        action_deselect();
        return true;
    }
  }
  if (document_ptr() != nullptr && document().selection().floating() &&
      (event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) == 0) {
    int dx = 0;
    int dy = 0;
    if (event->keyval == GDK_KEY_Left) {
      dx = -1;
    } else if (event->keyval == GDK_KEY_Right) {
      dx = 1;
    } else if (event->keyval == GDK_KEY_Up) {
      dy = -1;
    } else if (event->keyval == GDK_KEY_Down) {
      dy = 1;
    }
    if (dx != 0 || dy != 0) {
      if ((event->state & GDK_SHIFT_MASK) != 0) {
        dx *= 10;
        dy *= 10;
      }
      document().nudge_floating(dx, dy);
      return true;
    }
  }
  if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
    if (active_tool_ != nullptr && active_tool_->on_commit()) {
      return true;
    }
  }
  if (texting) {
    const bool ctrl = (event->state & GDK_CONTROL_MASK) != 0;
    const bool alt = (event->state & GDK_MOD1_MASK) != 0;
    const bool paste = ctrl && !alt && (event->keyval == GDK_KEY_v || event->keyval == GDK_KEY_V);
    const bool shift_insert =
        !ctrl && !alt && (event->state & GDK_SHIFT_MASK) != 0 && event->keyval == GDK_KEY_Insert;
    if ((!ctrl && !alt) || paste || shift_insert) {
      const std::string typed = event->string != nullptr ? event->string : "";
      if (active_tool_->on_key(event->keyval, mods_of(event->state), typed)) {
        return true;
      }
    }
    // Plain keys must not fall through to tool shortcuts while a text box is
    // open. Accelerators (Ctrl/Alt, other than paste) still propagate.
    if (!ctrl && !alt) {
      return false;
    }
  }
  if ((event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) != 0) {
    return false;
  }
  const guint32 ch = gdk_keyval_to_unicode(gdk_keyval_to_upper(event->keyval));
  std::vector<ToolKey> keys;
  keys.reserve(tools_.size());
  for (const auto& tool : tools_) {
    keys.push_back(ToolKey{tool->id(), tool->shortcut()});
  }
  const CanvasKeyResult resolved = resolve_canvas_key(
      static_cast<char>(ch), keys.data(), static_cast<int>(keys.size()),
      active_tool_ != nullptr ? active_tool_->id() : nullptr);
  if (resolved.action == CanvasKeyAction::SwapColors) {
    document().swap_colors();
    return true;
  }
  if (resolved.action == CanvasKeyAction::ResetColors) {
    document().reset_colors();
    return true;
  }
  if (resolved.action == CanvasKeyAction::SelectTool && resolved.tool_id != nullptr) {
    set_active_tool(resolved.tool_id);
    return true;
  }
  return false;
}

bool MainWindow::on_key_release(GdkEventKey* event) {
  if (event != nullptr && event->keyval == GDK_KEY_space) {
    canvas_.set_space_down(false);
  }
  return false;
}

void MainWindow::on_toggle_right_dock() {
  right_sidebar_.set_visible(!right_sidebar_.get_visible());
  // Dock show/hide changes the canvas pane size; force a resize pass then re-center.
  queue_resize();
  Glib::signal_idle().connect([this]() {
    canvas_.recenter_in_viewport();
    return false;
  });
}

void MainWindow::action_new() {
  NewImageDialog dialog(*this, prefs_.default_width, prefs_.default_height);
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const int width = dialog.image_width();
  const int height = dialog.image_height();
  if (width < 1 || height < 1) {
    return;
  }
  if (width > kHardMaxSide || height > kHardMaxSide) {
    Gtk::MessageDialog refuse(*this, "Images cannot be larger than 16384 pixels on a side.", false,
                              Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    refuse.run();
    return;
  }
  if (dialog.oversized()) {
    Gtk::MessageDialog warn(*this,
                            "This canvas is larger than 8192 pixels on a side and may use a lot of memory.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
    if (warn.run() != Gtk::RESPONSE_OK) {
      return;
    }
  }
  new_document(width, height, dialog.background_color());
  show_status("New canvas");
}

void MainWindow::action_open() {
  const std::string path = choose_open_path();
  if (path.empty()) {
    return;
  }
  open_path(path);
}

bool MainWindow::open_path(const std::string& path, bool force_replace) {
  if (path.empty()) {
    return false;
  }
  if (format_from_path(path) == ImageFormat::Ora) {
    LoadedOra loaded = load_ora(path);
    if (!loaded.ok()) {
      Gtk::MessageDialog err(*this, "Could not open OpenRaster file.", false, Gtk::MESSAGE_ERROR,
                             Gtk::BUTTONS_OK, true);
      err.set_secondary_text(loaded.error);
      err.run();
      return false;
    }
    if (loaded.warn_size) {
      Gtk::MessageDialog warn(*this,
                              "This image is larger than 8192 pixels on a side and may use a lot of memory.",
                              false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
      if (warn.run() != Gtk::RESPONSE_OK) {
        return false;
      }
    }
    if (loaded.warn_layers) {
      Gtk::MessageDialog warn(*this, "This file has more than 64 layers and may use a lot of memory.",
                              false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
      if (warn.run() != Gtk::RESPONSE_OK) {
        return false;
      }
    }
    std::vector<std::unique_ptr<Layer>> layers;
    layers.reserve(loaded.layers.size());
    for (const auto& snap : loaded.layers) {
      layers.push_back(layer_from_snapshot(snap));
    }
    auto doc = Document::create(loaded.width, loaded.height, Color::transparent(),
                                loaded.layers.front().name);
    doc->replace_stack(loaded.width, loaded.height, std::move(layers),
                       static_cast<int>(loaded.layers.size()) - 1);
    if (loaded.has_stack) {
      doc->set_ora_stack(loaded.stack);
    }
    doc->set_path(path);
    doc->mark_clean();
    if (force_replace) {
      if (active_tool_ != nullptr) {
        active_tool_->on_cancel();
      }
      doc->history().set_depth(prefs_.undo_limit);
      detach_document();
      workspace_.replace_active(std::move(doc));
      attach_active_document();
      rebuild_tabs();
    } else if (!adopt_document(std::move(doc), true, LivePath::Open)) {
      return false;
    }
    remember_recent(path);
    show_status("Opened " + Glib::path_get_basename(path));
    return true;
  }
  LoadedImage loaded = load_flat_image(path);
  if (!loaded.ok()) {
    Gtk::MessageDialog err(*this, "Could not open image.", false, Gtk::MESSAGE_ERROR,
                           Gtk::BUTTONS_OK, true);
    err.set_secondary_text(loaded.error);
    err.run();
    return false;
  }
  if (loaded.animated) {
    Gtk::MessageDialog warn(*this,
                            "This GIF has more than one frame. Only the first frame was opened.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
    warn.run();
  }
  if (loaded.width > kSoftMaxSide || loaded.height > kSoftMaxSide) {
    Gtk::MessageDialog warn(*this,
                            "This image is larger than 8192 pixels on a side and may use a lot of memory.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
    if (warn.run() != Gtk::RESPONSE_OK) {
      return false;
    }
  }
  auto doc = Document::create(loaded.width, loaded.height, Color::transparent(), loaded.layer_name);
  doc->layers().active_layer().write_rect(Rect{0, 0, loaded.width, loaded.height},
                                          loaded.rgba.data());
  doc->set_path(path);
  doc->mark_clean();
  if (force_replace) {
    if (active_tool_ != nullptr) {
      active_tool_->on_cancel();
    }
    doc->history().set_depth(prefs_.undo_limit);
    detach_document();
    workspace_.replace_active(std::move(doc));
    attach_active_document();
    rebuild_tabs();
  } else if (!adopt_document(std::move(doc), true, LivePath::Open)) {
    return false;
  }
  remember_recent(path);
  show_status("Opened " + Glib::path_get_basename(path));
  return true;
}
void MainWindow::action_save() {
  if (document().path().empty() || format_from_path(document().path()) == ImageFormat::Unknown) {
    action_save_as();
    return;
  }
  if (save_to_path(document().path(), format_from_path(document().path()))) {
    document().mark_clean();
    remember_recent(document().path());
    clear_document_recovery(document());
    update_chrome();
    show_status("Saved");
  }
}

void MainWindow::action_save_as() {
  std::string path;
  ImageFormat format = ImageFormat::Ora;
  if (!choose_save_path(path, format)) {
    return;
  }
  bool also_ora = false;
  if (format != ImageFormat::Ora && document().layers().count() > 1) {
    Gtk::MessageDialog warn(*this,
                            "This document has multiple layers. Saving a flat file will flatten visible layers.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_NONE, true);
    warn.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    warn.add_button("Flatten only", Gtk::RESPONSE_NO);
    warn.add_button("Flatten and keep .ora", Gtk::RESPONSE_YES);
    const int response = warn.run();
    if (response == Gtk::RESPONSE_CANCEL) {
      return;
    }
    also_ora = response == Gtk::RESPONSE_YES;
  }
  if (save_to_path(path, format)) {
    bool ora_ok = true;
    if (also_ora) {
      const std::string ora_path = replace_path_extension(path, ".ora");
      std::string error;
      ora_ok = save_ora(ora_path, document(), error);
      if (!ora_ok) {
        Gtk::MessageDialog err(*this, "Saved the flat file, but could not write the .ora copy.",
                               false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
        err.set_secondary_text(error);
        err.run();
      }
    }
    const KeepOraDecision kept = decide_keep_ora(true, also_ora, ora_ok);
    if (!kept.adopt_path) {
      update_chrome();
      return;
    }
    document().set_path(path);
    if (kept.mark_clean) {
      document().mark_clean();
    }
    remember_recent(path);
    if (kept.clear_recovery) {
      clear_document_recovery(document());
    }
    update_chrome();
    show_status("Saved");
  }
}

bool MainWindow::active_document_holds_live_work(const Document* document) {
  if (document == nullptr || document != document_ptr()) {
    return false;
  }
  return live_edit_blocks_silent_close(classify_live(active_tool_, document));
}

bool MainWindow::confirm_close() {
  const int original = workspace_.active_index();
  std::vector<int> order;
  order.reserve(static_cast<std::size_t>(workspace_.count()));
  if (original >= 0) {
    order.push_back(original);
  }
  for (int i = 0; i < workspace_.count(); ++i) {
    if (i != original) {
      order.push_back(i);
    }
  }
  for (int i : order) {
    if (i < 0 || i >= workspace_.count()) {
      continue;
    }
    Document& doc = workspace_.at(i);
    if (!doc.dirty() && !active_document_holds_live_work(&doc)) {
      continue;
    }
    if (!confirm_lose_document(doc)) {
      if (workspace_.active_index() != original && original >= 0 && original < workspace_.count()) {
        workspace_.set_active(original);
        attach_active_document();
      }
      return false;
    }
  }
  return true;
}

bool MainWindow::on_delete_event(GdkEventAny* event) {
  if (!confirm_close()) {
    return true;
  }
  return Gtk::ApplicationWindow::on_delete_event(event);
}

bool MainWindow::confirm_lose_changes() {
  return confirm_lose_document(document());
}

bool MainWindow::confirm_lose_document(Document& document) {
  if (!document.dirty() && !active_document_holds_live_work(&document)) {
    return true;
  }
  Glib::ustring name = document.path().empty() ? "Untitled" : Glib::path_get_basename(document.path());
  Gtk::MessageDialog dialog(*this, "Save changes to " + name + "?", false,
                            Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Discard", Gtk::RESPONSE_NO);
  dialog.add_button("_Save", Gtk::RESPONSE_YES);
  const int response = dialog.run();
  CloseAnswer answer = CloseAnswer::Cancel;
  if (response == Gtk::RESPONSE_YES) {
    answer = CloseAnswer::Save;
  } else if (response == Gtk::RESPONSE_NO) {
    answer = CloseAnswer::Discard;
  }
  const LiveEditAction edit = live_edit_for_close(answer);
  if (edit == LiveEditAction::Leave) {
    return false;
  }
  const int idx = workspace_.index_of(&document);
  const bool is_active = idx >= 0 && idx == workspace_.active_index();
  const bool needs_chooser =
      document.path().empty() || format_from_path(document.path()) == ImageFormat::Unknown;
  const CloseSavePlan plan = plan_close_save(answer, needs_chooser, false);
  if (plan.discard_edit) {
    if (is_active && active_tool_ != nullptr) {
      active_tool_->on_cancel();
    }
    crash_recovery::clear_file(crash_recovery::autosave_path_for(document.recovery_id()));
    crash_recovery::clear_after_discard(other_documents_dirty(&document));
    return true;
  }
  if (!plan.commit_then_write && answer != CloseAnswer::Save) {
    return false;
  }
  // A path is not known yet when Save As must run. Stamp only inside
  // save_to_path, after the chooser has accepted a name.
  if (!is_active && idx >= 0) {
    if (!preserve_live_edits_for_tab_switch()) {
      return false;
    }
    workspace_.set_active(idx);
    attach_active_document();
  }
  action_save();
  return !document.dirty();
}

bool MainWindow::layer_has_transparency() const {
  std::vector<std::uint8_t> flat;
  composite_visible(flat);
  const int n = document().width() * document().height();
  for (int i = 0; i < n; ++i) {
    if (flat[static_cast<std::size_t>(i) * 4 + 3] != 255) {
      return true;
    }
  }
  return false;
}

void MainWindow::composite_visible(std::vector<std::uint8_t>& dest) const {
  const int w = document().width();
  const int h = document().height();
  dest.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
  document().layers().composite_rect(dest.data(), w * 4, Rect{0, 0, w, h});
}

bool MainWindow::confirm_large_canvas(int width, int height) {
  return warn_size(width, height);
}

bool MainWindow::commit_live_edits() {
  if (document_ptr() == nullptr) {
    return true;
  }
  return settle_tool_live(active_tool_, document_ptr(), LivePath::Save) != SettleResult::Blocked;
}

bool MainWindow::save_to_path(const std::string& path, ImageFormat format) {
  // Reject and warn before any stamp. Cancelling a chooser never reaches
  // here; cancelling one of these dialogs must leave the live edit up too.
  if (format == ImageFormat::Gif) {
    Gtk::MessageDialog err(*this, "GIF save is not supported.", false, Gtk::MESSAGE_ERROR,
                           Gtk::BUTTONS_OK, true);
    err.run();
    return false;
  }
  if (format != ImageFormat::Ora) {
    const bool multi = document().layers().count() > 1;
    std::vector<std::uint8_t> preview;
    composite_visible(preview);
    bool transparent = false;
    const int pixel_count = document().width() * document().height();
    for (int i = 0; i < pixel_count; ++i) {
      if (preview[static_cast<std::size_t>(i) * 4 + 3] != 255) {
        transparent = true;
        break;
      }
    }
    if (!transparent && document().selection().floating() &&
        document().selection().float_pixels() != nullptr) {
      const int fw = document().selection().float_w();
      const int fh = document().selection().float_h();
      const std::uint8_t* pixels = document().selection().float_pixels();
      const std::uint8_t* coverage = document().selection().float_coverage();
      for (int i = 0; i < fw * fh; ++i) {
        if (coverage != nullptr && coverage[i] == 0) {
          continue;
        }
        if (pixels[static_cast<std::size_t>(i) * 4 + 3] != 255) {
          transparent = true;
          break;
        }
      }
    }
    if (multi && format == ImageFormat::Png) {
      Gtk::MessageDialog warn(*this,
                              "PNG will flatten visible layers (alpha is kept).",
                              false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
      if (warn.run() != Gtk::RESPONSE_OK) {
        return false;
      }
    }
    if (format == ImageFormat::Jpeg && (transparent || multi)) {
      Gtk::MessageDialog warn(*this,
                              "JPEG cannot store transparency or layers. The image will be flattened onto white.",
                              false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
      if (warn.run() != Gtk::RESPONSE_OK) {
        return false;
      }
    }
    if (format == ImageFormat::Bmp && (transparent || multi)) {
      Gtk::MessageDialog warn(*this,
                              "BMP cannot store transparency or layers. The image will be flattened onto white.",
                              false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
      if (warn.run() != Gtk::RESPONSE_OK) {
        return false;
      }
    }
  }
  if (!commit_live_edits()) {
    return false;
  }
  if (format == ImageFormat::Ora) {
    std::string error;
    if (!save_ora(path, document(), error)) {
      Gtk::MessageDialog err(*this, "Could not save OpenRaster file.", false, Gtk::MESSAGE_ERROR,
                             Gtk::BUTTONS_OK, true);
      err.set_secondary_text(error);
      err.run();
      return false;
    }
    return true;
  }

  std::vector<std::uint8_t> flat;
  composite_visible(flat);
  std::string error;
  if (!save_flat_image(path, format, flat.data(), document().width(), document().height(),
                       document().width() * 4, jpeg_quality_, error)) {
    Gtk::MessageDialog err(*this, "Could not save image.", false, Gtk::MESSAGE_ERROR,
                           Gtk::BUTTONS_OK, true);
    err.set_secondary_text(error);
    err.run();
    return false;
  }
  return true;
}

namespace {

Glib::RefPtr<Gdk::Pixbuf> pixbuf_from_preview(const LoadedImage& image) {
  if (!image.ok()) {
    return {};
  }
  auto pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, image.width, image.height);
  if (!pixbuf) {
    return {};
  }
  guint8* dst = pixbuf->get_pixels();
  const int stride = pixbuf->get_rowstride();
  const int channels = pixbuf->get_n_channels();
  for (int y = 0; y < image.height; ++y) {
    const std::uint8_t* src =
        image.rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) * 4;
    guint8* row = dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < image.width; ++x) {
      guint8* pixel = row + static_cast<std::size_t>(x) * static_cast<std::size_t>(channels);
      pixel[0] = src[static_cast<std::size_t>(x) * 4 + 0];
      pixel[1] = src[static_cast<std::size_t>(x) * 4 + 1];
      pixel[2] = src[static_cast<std::size_t>(x) * 4 + 2];
      if (channels > 3) {
        pixel[3] = src[static_cast<std::size_t>(x) * 4 + 3];
      }
    }
  }
  return pixbuf;
}

}  // namespace

std::string MainWindow::choose_open_path() {
  Gtk::FileChooserDialog dialog(*this, "Open Image", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Open", Gtk::RESPONSE_ACCEPT);
  Gtk::Image preview;
  preview.set_margin_start(8);
  preview.set_margin_end(8);
  preview.set_margin_top(8);
  preview.set_margin_bottom(8);
  preview.set_halign(Gtk::ALIGN_CENTER);
  preview.set_valign(Gtk::ALIGN_CENTER);
  dialog.set_preview_widget(preview);
  dialog.set_use_preview_label(false);
  dialog.set_preview_widget_active(false);
  dialog.signal_update_preview().connect([&dialog, &preview]() {
    try {
      preview.clear();
      const std::string path = dialog.get_preview_filename();
      LoadedImage image;
      if (path.empty() || !load_image_preview(path, 240, image)) {
        dialog.set_preview_widget_active(false);
        return;
      }
      const auto pixbuf = pixbuf_from_preview(image);
      if (!pixbuf) {
        dialog.set_preview_widget_active(false);
        return;
      }
      preview.set(pixbuf);
      dialog.set_preview_widget_active(true);
    } catch (const Glib::Error&) {
      preview.clear();
      dialog.set_preview_widget_active(false);
    }
  });
  auto all = Gtk::FileFilter::create();
  all->set_name("Images");
  all->add_pattern("*.ora");
  all->add_pattern("*.png");
  all->add_pattern("*.jpg");
  all->add_pattern("*.jpeg");
  all->add_pattern("*.bmp");
  all->add_pattern("*.gif");
  dialog.add_filter(all);
  auto ora = Gtk::FileFilter::create();
  ora->set_name("OpenRaster (*.ora)");
  ora->add_pattern("*.ora");
  dialog.add_filter(ora);
  auto png = Gtk::FileFilter::create();
  png->set_name("PNG");
  png->add_pattern("*.png");
  dialog.add_filter(png);
  auto jpeg = Gtk::FileFilter::create();
  jpeg->set_name("JPEG");
  jpeg->add_pattern("*.jpg");
  jpeg->add_pattern("*.jpeg");
  dialog.add_filter(jpeg);
  auto bmp = Gtk::FileFilter::create();
  bmp->set_name("BMP");
  bmp->add_pattern("*.bmp");
  dialog.add_filter(bmp);
  auto gif = Gtk::FileFilter::create();
  gif->set_name("GIF");
  gif->add_pattern("*.gif");
  dialog.add_filter(gif);
  if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
    return {};
  }
  return dialog.get_filename();
}

bool MainWindow::choose_save_path(std::string& path, ImageFormat& format) {
  Gtk::FileChooserDialog dialog(*this, "Save Image", Gtk::FILE_CHOOSER_ACTION_SAVE);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Save", Gtk::RESPONSE_ACCEPT);
  dialog.set_do_overwrite_confirmation(true);
  // Default GTK file chooser mirrors the parent size on many themes — pin it.
  dialog.set_default_size(620, 400);
  dialog.signal_map().connect([&dialog]() { dialog.resize(620, 400); });
  auto ora = Gtk::FileFilter::create();
  ora->set_name("OpenRaster project (*.ora)");
  ora->add_pattern("*.ora");
  dialog.add_filter(ora);
  auto png = Gtk::FileFilter::create();
  png->set_name("PNG image (*.png)");
  png->add_pattern("*.png");
  dialog.add_filter(png);
  auto jpeg = Gtk::FileFilter::create();
  jpeg->set_name("JPEG image (*.jpg)");
  jpeg->add_pattern("*.jpg");
  jpeg->add_pattern("*.jpeg");
  dialog.add_filter(jpeg);
  auto bmp = Gtk::FileFilter::create();
  bmp->set_name("BMP image (*.bmp)");
  bmp->add_pattern("*.bmp");
  dialog.add_filter(bmp);
  if (!document().path().empty()) {
    dialog.set_filename(document().path());
  } else {
    dialog.set_current_name("untitled.ora");
  }
  auto sync_extension = [&dialog]() {
    auto filter = dialog.get_filter();
    if (!filter) {
      return;
    }
    const Glib::ustring name = filter->get_name();
    const char* ext = ".ora";
    if (name.find("JPEG") != Glib::ustring::npos) {
      ext = ".jpg";
    } else if (name.find("BMP") != Glib::ustring::npos) {
      ext = ".bmp";
    } else if (name.find("PNG") != Glib::ustring::npos) {
      ext = ".png";
    } else if (name.find("OpenRaster") != Glib::ustring::npos) {
      ext = ".ora";
    } else {
      return;
    }
    std::string cur = dialog.get_current_name();
    if (cur.empty()) {
      const std::string full = dialog.get_filename();
      if (!full.empty()) {
        const auto slash = full.find_last_of('/');
        cur = slash == std::string::npos ? full : full.substr(slash + 1);
      }
    }
    if (cur.empty()) {
      cur = "untitled";
    }
    auto dot = cur.find_last_of('.');
    if (dot != std::string::npos) {
      cur = cur.substr(0, dot);
    }
    cur += ext;
    dialog.set_current_name(cur);
  };
  dialog.property_filter().signal_changed().connect(sync_extension);
  Gtk::Box extra(Gtk::ORIENTATION_HORIZONTAL, 8);
  extra.set_border_width(4);
  auto* qlabel = Gtk::manage(new Gtk::Label("JPEG quality"));
  auto* qspin = Gtk::manage(new Gtk::SpinButton());
  qspin->set_range(1, 100);
  qspin->set_increments(1, 10);
  qspin->set_digits(0);
  qspin->set_value(jpeg_quality_);
  extra.pack_start(*qlabel, Gtk::PACK_SHRINK);
  extra.pack_start(*qspin, Gtk::PACK_SHRINK);
  extra.show_all();
  dialog.set_extra_widget(extra);
  if (dialog.run() != Gtk::RESPONSE_ACCEPT) {
    return false;
  }
  jpeg_quality_ = qspin->get_value_as_int();
  path = dialog.get_filename();
  format = format_from_path(path);
  ImageFormat forced = ImageFormat::Unknown;
  auto filter = dialog.get_filter();
  if (filter) {
    const Glib::ustring name = filter->get_name();
    if (name.find("JPEG") != Glib::ustring::npos) {
      forced = ImageFormat::Jpeg;
    } else if (name.find("BMP") != Glib::ustring::npos) {
      forced = ImageFormat::Bmp;
    } else if (name.find("PNG") != Glib::ustring::npos) {
      forced = ImageFormat::Png;
    } else if (name.find("OpenRaster") != Glib::ustring::npos) {
      forced = ImageFormat::Ora;
    }
  }
  if (format == ImageFormat::Unknown) {
    format = forced == ImageFormat::Unknown ? ImageFormat::Ora : forced;
    path = replace_path_extension(path, format_extension(format));
  } else if (forced != ImageFormat::Unknown && forced != format) {
    format = forced;
    path = replace_path_extension(path, format_extension(format));
  }
  // Never write a flat image over an existing .ora path.
  if (format != ImageFormat::Ora && format_from_path(path) == ImageFormat::Ora) {
    path = replace_path_extension(path, format_extension(format));
  }
  if (format != ImageFormat::Ora && !document().path().empty() &&
      format_from_path(document().path()) == ImageFormat::Ora && path == document().path()) {
    path = replace_path_extension(path, format_extension(format));
  }
  if (format_from_path(path) == ImageFormat::Gif) {
    if (forced == ImageFormat::Unknown) {
      forced = ImageFormat::Png;
    }
    format = forced;
    path = replace_path_extension(path, format_extension(format));
  }
  return true;
}


namespace {

bool pixbuf_to_tight_rgba(const Glib::RefPtr<Gdk::Pixbuf>& pixbuf, int& w, int& h,
                          std::vector<std::uint8_t>& rgba) {
  w = 0;
  h = 0;
  rgba.clear();
  if (!pixbuf) {
    return false;
  }
  w = pixbuf->get_width();
  h = pixbuf->get_height();
  if (w < 1 || h < 1) {
    return false;
  }
  auto rgba_buf = pixbuf->add_alpha(false, 0, 0, 0);
  rgba.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0);
  const int src_stride = rgba_buf->get_rowstride();
  const std::uint8_t* src = rgba_buf->get_pixels();
  const int nch = rgba_buf->get_n_channels();
  for (int y = 0; y < h; ++y) {
    const std::uint8_t* srow = src + static_cast<std::size_t>(y) * src_stride;
    std::uint8_t* drow = rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4;
    for (int x = 0; x < w; ++x) {
      const std::uint8_t* p = srow + static_cast<std::size_t>(x) * nch;
      drow[x * 4 + 0] = p[0];
      drow[x * 4 + 1] = p[1];
      drow[x * 4 + 2] = p[2];
      drow[x * 4 + 3] = nch >= 4 ? p[3] : 255;
    }
  }
  return true;
}

}  // namespace

void MainWindow::copy_selection_to_clipboard() {
  if (document().selection().empty()) {
    return;
  }
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgba;
  std::vector<std::uint8_t> coverage;
  copy_selection_rgba(document().layers().active_layer(), document().selection(), document().width(),
                      document().height(), w, h, rgba, &coverage);
  if (w < 1 || h < 1 || rgba.empty()) {
    return;
  }
  clip_w_ = w;
  clip_h_ = h;
  clip_rgba_ = rgba;
  clip_coverage_ = std::move(coverage);
  auto pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, w, h);
  const int dst_stride = pixbuf->get_rowstride();
  std::uint8_t* dst = pixbuf->get_pixels();
  for (int y = 0; y < h; ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * dst_stride,
                rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4,
                static_cast<std::size_t>(w) * 4);
  }
  Gtk::Clipboard::get()->set_image(pixbuf);
  show_status("Copied");
}

void MainWindow::copy_merged_to_clipboard() {
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgba;
  std::vector<std::uint8_t> coverage;
  copy_merged_rgba(document().layers(), document().selection(), document().width(),
                   document().height(), w, h, rgba, &coverage);
  if (w < 1 || h < 1 || rgba.empty()) {
    return;
  }
  clip_w_ = w;
  clip_h_ = h;
  clip_rgba_ = rgba;
  clip_coverage_ = std::move(coverage);
  auto pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, w, h);
  const int dst_stride = pixbuf->get_rowstride();
  std::uint8_t* dst = pixbuf->get_pixels();
  for (int y = 0; y < h; ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * dst_stride,
                rgba.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4,
                static_cast<std::size_t>(w) * 4);
  }
  Gtk::Clipboard::get()->set_image(pixbuf);
  show_status("Copied merged");
}

bool MainWindow::paste_from_clipboard() {
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgba;
  if (!pixbuf_to_tight_rgba(Gtk::Clipboard::get()->wait_for_image(), w, h, rgba)) {
    report_blocked("Clipboard has no image");
    return false;
  }
  int px = 0;
  int py = 0;
  if (!canvas_.last_pointer(px, py)) {
    canvas_.viewport_center_canvas(px, py);
  }
  std::vector<std::uint8_t> coverage;
  if (w == clip_w_ && h == clip_h_ && clip_rgba_ == rgba && !clip_coverage_.empty()) {
    coverage = clip_coverage_;
  }
  document().paste_floating(px, py, w, h, std::move(rgba), std::move(coverage));
  set_active_tool("rect-select");
  show_status("Pasted");
  return true;
}

void MainWindow::action_paste_into_new() {
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgba;
  if (!pixbuf_to_tight_rgba(Gtk::Clipboard::get()->wait_for_image(), w, h, rgba)) {
    report_blocked("Clipboard has no image");
    return;
  }
  const std::uint8_t* coverage = nullptr;
  if (w == clip_w_ && h == clip_h_ && clip_rgba_ == rgba &&
      clip_coverage_.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h)) {
    coverage = clip_coverage_.data();
  }
  auto pasted = Document::from_masked_paste(w, h, rgba.data(), coverage);
  if (!adopt_document(std::move(pasted), false, LivePath::Paste)) {
    return;
  }
  show_status("Pasted into new image");
}

void MainWindow::action_cut() {
  if (document().selection().empty()) {
    return;
  }
  copy_selection_to_clipboard();
  document().delete_selection();
}

void MainWindow::action_copy() {
  copy_selection_to_clipboard();
}

void MainWindow::action_copy_merged() {
  copy_merged_to_clipboard();
}

void MainWindow::action_paste() {
  paste_from_clipboard();
}

void MainWindow::action_delete() {
  if (focus_is_editable()) {
    return;
  }
  document().delete_selection();
}

void MainWindow::action_duplicate() {
  document().duplicate_selection();
  set_active_tool("rect-select");
}

void MainWindow::action_select_all() {
  document().select_all();
}

void MainWindow::action_deselect() {
  if (!commit_live_edits()) {
    return;
  }
  document().deselect();
}

void MainWindow::action_invert_selection() {
  document().invert_selection();
}

void MainWindow::action_zoom_fit() {
  canvas_.zoom_fit();
}

void MainWindow::action_toggle_grid() {
  canvas_.set_grid_visible(!canvas_.grid_visible());
  show_status(canvas_.grid_visible() ? "Grid on" : "Grid off");
}

bool MainWindow::warn_size(int width, int height) {
  if (width < 1 || height < 1) {
    return false;
  }
  if (width > kHardMaxSide || height > kHardMaxSide) {
    Gtk::MessageDialog refuse(*this, "Images cannot be larger than 16384 pixels on a side.", false,
                              Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK, true);
    refuse.run();
    return false;
  }
  if (width > kSoftMaxSide || height > kSoftMaxSide) {
    Gtk::MessageDialog warn(*this,
                            "This canvas is larger than 8192 pixels on a side and may use a lot of memory.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
    if (warn.run() != Gtk::RESPONSE_OK) {
      return false;
    }
  }
  return true;
}

void MainWindow::commit_buffer_change(const char* name, int new_w, int new_h,
                                      const std::uint8_t* rgba, int stride) {
  const Layer& layer = document().layers().active_layer();
  auto cmd = LayerBufferCommand::from_buffers(name, document().width(), document().height(),
                                              layer.pixels(), layer.stride(), new_w, new_h, rgba,
                                              stride, document().layers().active_index());
  document().commit(std::move(cmd));
  canvas_.refresh_size();
  canvas_.invalidate_all();
}

void MainWindow::commit_stack_transform(const char* name, const StackXform& xform) {
  auto old_layers = document().snapshot_layers();
  auto cmd = std::make_unique<AllLayersBufferCommand>(
      name, std::move(old_layers), document().layers().active_index(), xform);
  document().commit(std::move(cmd));
  canvas_.refresh_size();
  canvas_.invalidate_all();
}

void MainWindow::action_canvas_size() {
  if (!commit_live_edits()) {
    return;
  }
  CanvasSizeDialog dialog(*this, document().width(), document().height());
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const int nw = dialog.image_width();
  const int nh = dialog.image_height();
  if (!warn_size(nw, nh)) {
    return;
  }
  const Color fill = dialog.fill_color(document().background());
  StackXform xform;
  xform.kind = StackXformKind::ResizeCanvas;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = nw;
  xform.new_h = nh;
  xform.fill = fill;
  commit_stack_transform("Canvas size", xform);
}

void MainWindow::action_scale() {
  if (!commit_live_edits()) {
    return;
  }
  ScaleImageDialog dialog(*this, document().width(), document().height());
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const int nw = dialog.image_width();
  const int nh = dialog.image_height();
  if (!warn_size(nw, nh)) {
    return;
  }
  StackXform xform;
  xform.kind = StackXformKind::Scale;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = nw;
  xform.new_h = nh;
  xform.nearest = dialog.nearest();
  commit_stack_transform("Scale", xform);
}

void MainWindow::action_crop() {
  if (!commit_live_edits()) {
    return;
  }
  if (!document().crop_to_selection()) {
    return;
  }
  canvas_.refresh_size();
  canvas_.invalidate_all();
}

void MainWindow::action_autocrop() {
  if (!commit_live_edits()) {
    return;
  }
  std::vector<std::uint8_t> flat;
  composite_visible(flat);
  const int canvas_w = document().width();
  const int canvas_h = document().height();
  const Rect local =
      autocrop_bounds(flat.data(), canvas_w, canvas_h, canvas_w * 4);
  if (local.empty() ||
      (local.x == 0 && local.y == 0 && local.w == canvas_w && local.h == canvas_h)) {
    report_blocked("Nothing to autocrop");
    return;
  }
  const Rect r = local;
  StackXform xform;
  xform.kind = StackXformKind::Crop;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = r.w;
  xform.new_h = r.h;
  xform.crop = r;
  commit_stack_transform("Autocrop", xform);
}

void MainWindow::action_rotate_90() {
  if (!commit_live_edits()) {
    return;
  }
  StackXform xform;
  xform.kind = StackXformKind::Rotate90;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = document().height();
  xform.new_h = document().width();
  commit_stack_transform("Rotate 90", xform);
}

void MainWindow::action_rotate_180() {
  if (!commit_live_edits()) {
    return;
  }
  StackXform xform;
  xform.kind = StackXformKind::Rotate180;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = xform.old_w;
  xform.new_h = xform.old_h;
  commit_stack_transform("Rotate 180", xform);
}

void MainWindow::action_rotate_ccw() {
  if (!commit_live_edits()) {
    return;
  }
  StackXform xform;
  xform.kind = StackXformKind::Rotate270;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = document().height();
  xform.new_h = document().width();
  commit_stack_transform("Rotate 270", xform);
}

void MainWindow::action_flip_h() {
  if (document_ptr() != nullptr && document().selection().floating()) {
    document().selection().flip_horizontal();
    document().notify_invalidated(document().selection().dirty_union());
    document().notify_changed();
    canvas_.invalidate_all();
    return;
  }
  if (!commit_live_edits()) {
    return;
  }
  StackXform xform;
  xform.kind = StackXformKind::FlipH;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = xform.old_w;
  xform.new_h = xform.old_h;
  commit_stack_transform("Flip horizontal", xform);
}

void MainWindow::action_flip_v() {
  if (document_ptr() != nullptr && document().selection().floating()) {
    document().selection().flip_vertical();
    document().notify_invalidated(document().selection().dirty_union());
    document().notify_changed();
    canvas_.invalidate_all();
    return;
  }
  if (!commit_live_edits()) {
    return;
  }
  StackXform xform;
  xform.kind = StackXformKind::FlipV;
  xform.old_w = document().width();
  xform.old_h = document().height();
  xform.new_w = xform.old_w;
  xform.new_h = xform.old_h;
  commit_stack_transform("Flip vertical", xform);
}

void MainWindow::action_clear() {
  if (document().active_locked()) {
    report_blocked("Layer is locked");
    return;
  }
  if (!commit_live_edits()) {
    return;
  }
  document().deselect();
  Layer& layer = document().layers().active_layer();
  Layer before(layer.width(), layer.height(), Color::transparent(), "before");
  before.copy_from(layer);
  layer.fill(document().background());
  auto cmd = PixelPatchCommand::from_layers(before, layer, Rect{0, 0, layer.width(), layer.height()},
                                            "Clear", document().layers().active_index());
  if (cmd && !cmd->empty()) {
    document().commit(std::move(cmd));
  }
}


void MainWindow::action_layer_new() {
  if (document().layers().count() >= kSoftMaxLayers) {
    Gtk::MessageDialog warn(*this,
                            "This document has 64 or more layers and may use a lot of memory.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
    if (warn.run() != Gtk::RESPONSE_OK) {
      return;
    }
  }
  if (!document().add_layer()) {
    return;
  }
  show_status("Added layer");
}

void MainWindow::action_layer_duplicate() {
  if (!document().duplicate_layer()) {
    return;
  }
  show_status("Duplicated layer");
}

void MainWindow::action_layer_delete() {
  if (document().layers().count() <= 1) {
    show_status("Cannot delete the last layer");
    return;
  }
  if (!document().delete_layer()) {
    return;
  }
  show_status("Deleted layer");
}

void MainWindow::action_layer_raise() {
  if (!document().raise_layer()) {
    return;
  }
  show_status("Raised layer");
}

void MainWindow::action_layer_lower() {
  if (!document().lower_layer()) {
    return;
  }
  show_status("Lowered layer");
}

void MainWindow::action_layer_merge_down() {
  if (document().width() > kSoftMaxSide || document().height() > kSoftMaxSide) {
    show_status("Merging layers…");
  }
  const bool can_merge =
      document().layers().count() >= 2 && document().layers().active_index() > 0;
  if (!document().merge_down()) {
    if (!can_merge) {
      show_status("Nothing below to merge");
    }
    return;
  }
  show_status("Merged down");
}

void MainWindow::action_layer_flatten() {
  if (document().layers().count() <= 1) {
    return;
  }
  if (!commit_live_edits()) {
    return;
  }
  if (!document().flatten()) {
    return;
  }
  show_status("Flattened");
}

void MainWindow::action_layer_properties() {
  layers_panel_.show_properties();
}


std::string MainWindow::tab_title(const Document& document) const {
  Glib::ustring name = document.path().empty() ? "Untitled" : Glib::path_get_basename(document.path());
  if (document.dirty()) {
    name += "*";
  }
  return name;
}

void MainWindow::rebuild_tabs() {
  switching_tabs_ = true;
  while (tab_bar_.get_n_pages() > 0) {
    tab_bar_.remove_page(0);
  }
  for (int i = 0; i < workspace_.count(); ++i) {
    auto* page = Gtk::manage(new Gtk::Box());
    auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
    auto* label = Gtk::manage(new Gtk::Label(tab_title(workspace_.at(i))));
    auto* close = Gtk::manage(new Gtk::Button());
    close->set_image_from_icon_name("window-close-symbolic", Gtk::ICON_SIZE_MENU);
    close->set_relief(Gtk::RELIEF_NONE);
    close->set_can_focus(false);
    close->set_tooltip_text("Close tab");
    Document* doc = &workspace_.at(i);
    close->signal_clicked().connect([this, doc]() {
      const int idx = workspace_.index_of(doc);
      if (idx >= 0) {
        close_document_at(idx);
      }
    });
    box->pack_start(*label, Gtk::PACK_SHRINK);
    box->pack_start(*close, Gtk::PACK_SHRINK);
    box->show_all();
    tab_bar_.append_page(*page, *box);
  }
  if (workspace_.count() > 1) {
    tab_bar_.show();
    tab_bar_.set_current_page(workspace_.active_index());
  } else {
    tab_bar_.hide();
  }
  switching_tabs_ = false;
}

void MainWindow::update_tab_labels() {
  if (tab_bar_.get_n_pages() != workspace_.count()) {
    rebuild_tabs();
    return;
  }
  for (int i = 0; i < workspace_.count(); ++i) {
    auto* widget = tab_bar_.get_tab_label(*tab_bar_.get_nth_page(i));
    if (auto* box = dynamic_cast<Gtk::Box*>(widget)) {
      auto children = box->get_children();
      if (!children.empty()) {
        if (auto* label = dynamic_cast<Gtk::Label*>(children[0])) {
          label->set_text(tab_title(workspace_.at(i)));
        }
      }
    }
  }
  if (workspace_.count() > 1) {
    tab_bar_.show();
  } else {
    tab_bar_.hide();
  }
}

bool MainWindow::close_document_at(int index) {
  if (index < 0 || index >= workspace_.count()) {
    return false;
  }
  const int original = workspace_.active_index();
  Document* target = &workspace_.at(index);
  if (!confirm_lose_document(*target)) {
    if (workspace_.active_index() != original && original >= 0 && original < workspace_.count()) {
      workspace_.set_active(original);
      attach_active_document();
    }
    return false;
  }
  index = workspace_.index_of(target);
  if (index < 0) {
    return false;
  }
  if (index == workspace_.active_index() && active_tool_ != nullptr) {
    active_tool_->on_cancel();
  }
  detach_document();
  if (workspace_.count() == 1) {
    auto blank = Document::create(prefs_.default_width, prefs_.default_height, Color::white());
    blank->history().set_depth(prefs_.undo_limit);
    workspace_.replace_active(std::move(blank));
  } else {
    workspace_.close(index);
  }
  attach_active_document();
  rebuild_tabs();
  return true;
}

void MainWindow::action_close_tab() {
  close_document_at(workspace_.active_index());
}

void MainWindow::apply_layer_effect(const char* name,
                                    const std::function<void(std::uint8_t*, int, int, int)>& fn) {
  if (document().active_locked()) {
    report_blocked("Layer is locked");
    return;
  }
  if (!commit_live_edits()) {
    return;
  }
  EffectPreview effect(document());
  if (!effect.valid()) {
    return;
  }
  effect.commit(name != nullptr ? name : "Adjust", fn);
}

bool MainWindow::run_adjust_dialog(LivePreviewDialog& dialog, const char* name,
                                   const std::function<EffectPreview::EffectFn()>& build_effect) {
  if (document().active_locked()) {
    report_blocked("Layer is locked");
    return false;
  }
  if (!commit_live_edits()) {
    return false;
  }
  EffectPreview effect(document());
  if (!effect.valid()) {
    return false;
  }
  effect_preview_ = &effect;
  // Live preview repaints the layer in place, straight from the snapshot, so
  // dragging never stacks and never pushes history.
  dialog.on_preview = [this, &effect, &build_effect]() {
    const EffectPreview::EffectFn fn = build_effect();
    if (fn) {
      effect.preview(fn);
    } else {
      effect.restore();
    }
    live_effect_preview_ = effect.previewing();
    canvas_.invalidate_all();
  };
  dialog.on_preview_reset = [this, &effect]() {
    effect.restore();
    live_effect_preview_ = false;
    canvas_.invalidate_all();
  };

  const int response = dialog.run();
  effect_preview_ = nullptr;
  live_effect_preview_ = false;
  const EffectPreview::EffectFn fn = build_effect();
  dialog.hide();
  // The callbacks captured locals; make sure a late signal cannot reach them.
  dialog.on_preview = nullptr;
  dialog.on_preview_reset = nullptr;

  if (response != Gtk::RESPONSE_OK || !fn) {
    // Cancel (or a no-op setting): put the original pixels back exactly.
    if (effect.restore()) {
      canvas_.invalidate_all();
      update_chrome();
    }
    return false;
  }
  // OK: discard the preview pixels and apply once, as a single undo step.
  const bool committed = effect.commit(name != nullptr ? name : "Adjust", fn);
  canvas_.invalidate_all();
  update_chrome();
  return committed;
}

void MainWindow::action_adjust_brightness() {
  BrightnessContrastDialog dialog(*this);
  run_adjust_dialog(dialog, "Brightness / Contrast", [&dialog]() -> EffectPreview::EffectFn {
    const int brightness = dialog.brightness();
    const int contrast = dialog.contrast();
    if (brightness == 0 && contrast == 0) {
      return nullptr;
    }
    return [brightness, contrast](std::uint8_t* px, int w, int h, int stride) {
      brightness_contrast_rgba(px, w, h, stride, brightness, contrast);
    };
  });
}

void MainWindow::action_adjust_invert() {
  apply_layer_effect("Invert", [](std::uint8_t* px, int w, int h, int stride) {
    invert_rgba(px, w, h, stride);
  });
}

void MainWindow::action_adjust_grayscale() {
  apply_layer_effect("Grayscale", [](std::uint8_t* px, int w, int h, int stride) {
    grayscale_rgba(px, w, h, stride);
  });
}

void MainWindow::action_adjust_hue() {
  HueSaturationDialog dialog(*this);
  run_adjust_dialog(dialog, "Hue / Saturation", [&dialog]() -> EffectPreview::EffectFn {
    const int hue = dialog.hue();
    const int saturation = dialog.saturation();
    if (hue == 0 && saturation == 0) {
      return nullptr;
    }
    return [hue, saturation](std::uint8_t* px, int w, int h, int stride) {
      hue_saturation_rgba(px, w, h, stride, hue, saturation);
    };
  });
}

void MainWindow::action_adjust_posterize() {
  PosterizeDialog dialog(*this);
  run_adjust_dialog(dialog, "Posterize", [&dialog]() -> EffectPreview::EffectFn {
    const int levels = dialog.levels();
    return [levels](std::uint8_t* px, int w, int h, int stride) {
      posterize_rgba(px, w, h, stride, levels);
    };
  });
}

void MainWindow::action_effect_blur() {
  BlurDialog dialog(*this);
  Rect region{};
  bool limited = false;
  if (document_ptr() != nullptr) {
    const Selection& sel = document().selection();
    if (!sel.empty() && !sel.inverted()) {
      const Layer& layer = document().layers().active_layer();
      const Rect bounds = sel.bounds();
      region = rect_intersect(Rect{bounds.x - layer.offset_x(), bounds.y - layer.offset_y(), bounds.w,
                                   bounds.h},
                              Rect{0, 0, layer.width(), layer.height()});
      limited = !region.empty();
    }
  }
  run_adjust_dialog(dialog, "Blur", [&dialog, limited, region]() -> EffectPreview::EffectFn {
    const int radius = dialog.radius();
    return [radius, limited, region](std::uint8_t* px, int w, int h, int stride) {
      std::vector<std::uint8_t> src(static_cast<std::size_t>(h) * static_cast<std::size_t>(stride));
      std::memcpy(src.data(), px, src.size());
      const Rect area = limited ? region : Rect{0, 0, w, h};
      box_blur_rect(src.data(), w, h, stride, px, stride, radius, area);
    };
  });
}

void MainWindow::action_effect_sharpen() {
  apply_layer_effect("Sharpen", [](std::uint8_t* px, int w, int h, int stride) {
    std::vector<std::uint8_t> src(static_cast<std::size_t>(h) * static_cast<std::size_t>(stride));
    std::memcpy(src.data(), px, src.size());
    sharpen_rgba(src.data(), w, h, stride, px, stride);
  });
}

void MainWindow::action_effect_emboss() {
  apply_layer_effect("Emboss", [](std::uint8_t* px, int w, int h, int stride) {
    std::vector<std::uint8_t> src(static_cast<std::size_t>(h) * static_cast<std::size_t>(stride));
    std::memcpy(src.data(), px, src.size());
    emboss_rgba(src.data(), w, h, stride, px, stride);
  });
}

void MainWindow::apply_preferences() {
  canvas_.set_checker_colors(prefs_.checker_light, prefs_.checker_dark);
  canvas_.set_grid_threshold(prefs_.grid_threshold);
  for (int i = 0; i < workspace_.count(); ++i) {
    workspace_.at(i).history().set_depth(prefs_.undo_limit);
  }
}

void MainWindow::action_preferences() {
  PreferencesDialog dialog(*this, prefs_);
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  dialog.apply_to(prefs_);
  if (!prefs_.save()) {
    Gtk::MessageDialog err(*this, "Could not save preferences.", false, Gtk::MESSAGE_WARNING,
                           Gtk::BUTTONS_OK, true);
    err.set_secondary_text(Preferences::config_path());
    err.run();
  }
  apply_preferences();
  update_chrome();
  show_status("Preferences saved");
}

void MainWindow::action_shortcuts() {
  Gtk::Dialog dialog("Keyboard Shortcuts", *this, true);
  dialog.add_button("_Close", Gtk::RESPONSE_CLOSE);
  dialog.set_default_response(Gtk::RESPONSE_CLOSE);
  auto* grid = Gtk::manage(new Gtk::Grid());
  grid->set_row_spacing(4);
  grid->set_column_spacing(24);
  grid->set_border_width(12);
  int nrows = 0;
  const ShortcutHelpRow* rows = shortcut_help_rows(nrows);
  for (int i = 0; i < nrows; ++i) {
    auto* action = Gtk::manage(new Gtk::Label(rows[i].action, Gtk::ALIGN_START));
    auto* keys = Gtk::manage(new Gtk::Label(rows[i].keys, Gtk::ALIGN_START));
    grid->attach(*action, 0, i, 1, 1);
    grid->attach(*keys, 1, i, 1, 1);
  }
  dialog.get_content_area()->pack_start(*grid, Gtk::PACK_SHRINK);
  dialog.show_all();
  dialog.run();
}

void MainWindow::action_about() {
  Gtk::AboutDialog dialog;
  dialog.set_transient_for(*this);
  dialog.set_program_name(actions::kProductName);
  dialog.set_version(actions::kVersion);
  dialog.set_comments("Graphic design software inspired by the greats.");
  dialog.set_copyright("Copyright © 2026 The Lunduke Journal");
  dialog.set_license_type(Gtk::LICENSE_GPL_3_0);
  dialog.set_wrap_license(true);
  dialog.set_website("https://lunduke.com");
  dialog.set_website_label("lunduke.com");
  dialog.set_logo_icon_name(actions::kAppId);
  dialog.run();
}

void MainWindow::action_print() {
  if (!commit_live_edits()) {
    return;
  }
  auto op = Gtk::PrintOperation::create();
  op->set_n_pages(1);
  op->set_embed_page_setup(true);
  op->set_unit(Gtk::UNIT_POINTS);
  op->signal_draw_page().connect([this](const Glib::RefPtr<Gtk::PrintContext>& ctx, int) {
    if (!ctx) {
      return;
    }
    const int w = document().width();
    const int h = document().height();
    if (w < 1 || h < 1) {
      return;
    }
    std::vector<std::uint8_t> flat;
    composite_visible(flat);
    auto surface = Cairo::ImageSurface::create(Cairo::FORMAT_ARGB32, w, h);
    std::uint8_t* dst = surface->get_data();
    const int dst_stride = surface->get_stride();
    for (int y = 0; y < h; ++y) {
      std::uint8_t* drow = dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(dst_stride);
      const std::uint8_t* srow =
          flat.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4;
      for (int x = 0; x < w; ++x) {
        const std::uint8_t* s = srow + static_cast<std::size_t>(x) * 4;
        const int a = s[3];
        drow[static_cast<std::size_t>(x) * 4 + 0] =
            static_cast<std::uint8_t>((static_cast<int>(s[2]) * a + 127) / 255);
        drow[static_cast<std::size_t>(x) * 4 + 1] =
            static_cast<std::uint8_t>((static_cast<int>(s[1]) * a + 127) / 255);
        drow[static_cast<std::size_t>(x) * 4 + 2] =
            static_cast<std::uint8_t>((static_cast<int>(s[0]) * a + 127) / 255);
        drow[static_cast<std::size_t>(x) * 4 + 3] = static_cast<std::uint8_t>(a);
      }
    }
    surface->mark_dirty();
    auto cr = ctx->get_cairo_context();
    const double pw = ctx->get_width();
    const double ph = ctx->get_height();
    const double scale = std::min(pw / static_cast<double>(w), ph / static_cast<double>(h));
    const double dw = static_cast<double>(w) * scale;
    const double dh = static_cast<double>(h) * scale;
    cr->save();
    cr->set_source_rgb(1.0, 1.0, 1.0);
    cr->paint();
    cr->translate((pw - dw) * 0.5, (ph - dh) * 0.5);
    cr->scale(scale, scale);
    cr->set_source(surface, 0, 0);
    cr->paint();
    cr->restore();
  });
  try {
    op->run(Gtk::PRINT_OPERATION_ACTION_PRINT_DIALOG, *this);
  } catch (const Gtk::PrintError& error) {
    Gtk::MessageDialog err(*this, "Could not print.", false, Gtk::MESSAGE_ERROR, Gtk::BUTTONS_OK,
                           true);
    err.set_secondary_text(error.what());
    err.run();
  }
}


void MainWindow::action_fullscreen() {
  const auto win = get_window();
  const bool is_full =
      win && (win->get_state() & Gdk::WINDOW_STATE_FULLSCREEN) != Gdk::WindowState(0);
  if (is_full) {
    unfullscreen();
  } else {
    fullscreen();
  }
}

void MainWindow::action_revert() {
  const std::string path = document().path();
  if (path.empty()) {
    show_status("Nothing to revert");
    return;
  }
  const bool live = active_tool_ != nullptr &&
                    (active_tool_->captures_keys() || active_tool_->has_uncommitted_preview() ||
                     active_tool_->is_stroking());
  if (document().dirty() || live || document().selection().floating()) {
    Gtk::MessageDialog dialog(*this, "Revert to last saved file? Unsaved changes will be lost.",
                              false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Revert", Gtk::RESPONSE_YES);
    if (dialog.run() != Gtk::RESPONSE_YES) {
      return;
    }
  }
  if (!open_path(path, true)) {
    return;
  }
  show_status("Reverted");
}

void MainWindow::remember_recent(const std::string& path) {
  if (path.empty() || path[0] != '/') {
    return;
  }
  if (crash_recovery::path_is_recovery(path)) {
    return;
  }
  prefs_.add_recent(path);
  rebuild_recent_menu();
}

void MainWindow::rebuild_recent_menu() {
  if (recent_item_ == nullptr) {
    return;
  }
  auto* submenu = Gtk::manage(new Gtk::Menu());
  if (prefs_.recent_files.empty()) {
    auto* empty = Gtk::manage(new Gtk::MenuItem("(empty)"));
    empty->set_sensitive(false);
    submenu->append(*empty);
  } else {
    int i = 0;
    for (const auto& path : prefs_.recent_files) {
      auto* item = Gtk::manage(new Gtk::MenuItem(path, false));
      item->signal_activate().connect([this, path]() { open_path(path); });
      submenu->append(*item);
      ++i;
      if (i >= 10) {
        break;
      }
    }
    submenu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
    auto* clear = Gtk::manage(new Gtk::MenuItem("Clear Recent"));
    clear->signal_activate().connect([this]() {
      prefs_.recent_files.clear();
      prefs_.save();
      rebuild_recent_menu();
    });
    submenu->append(*clear);
  }
  submenu->show_all();
  recent_item_->set_submenu(*submenu);
}

void MainWindow::offer_recovery() {
  const std::vector<std::string> files = crash_recovery::list_recovery_files();
  if (files.empty()) {
    return;
  }
  Glib::ustring secondary = "Each file is one document from a previous session:\n";
  for (const std::string& path : files) {
    secondary += Glib::path_get_basename(path);
    secondary += "\n";
  }
  Gtk::MessageDialog dialog(*this, "Recover unsaved documents?", false, Gtk::MESSAGE_QUESTION,
                            Gtk::BUTTONS_NONE, true);
  dialog.set_secondary_text(secondary);
  dialog.add_button("_Discard", Gtk::RESPONSE_NO);
  dialog.add_button("_Recover", Gtk::RESPONSE_YES);
  const int response = dialog.run();
  if (response == Gtk::RESPONSE_YES) {
    for (const std::string& path : files) {
      if (open_path(path) && document_ptr() != nullptr && recovery_file_consumed(true)) {
        document().set_path(std::string());
        document().set_dirty(true);
        crash_recovery::clear_file(path);
      } else {
        show_status("Recovery was not applied. The file was kept.");
      }
    }
  } else {
    for (const std::string& path : files) {
      crash_recovery::clear_file(path);
    }
    crash_recovery::clear();
  }
}

void MainWindow::start_recovery_save() {
  if (!recovery_slot_ || workspace_.count() < 1) {
    return;
  }
  const bool stroke = active_tool_ != nullptr && active_tool_->is_stroking();
  const bool text = active_tool_ != nullptr && active_tool_->captures_keys();
  const bool floating = document_ptr() != nullptr && document().selection().floating();
  const int active = workspace_.active_index();
  struct Job {
    OraSnapshot snapshot;
    std::string dest;
  };
  std::vector<Job> jobs;
  const gint64 now = g_get_monotonic_time();
  const bool idle_long = last_edit_us_ == 0 || (now - last_edit_us_) >= 120LL * G_USEC_PER_SEC;
  for (int i = 0; i < workspace_.count(); ++i) {
    Document& doc = workspace_.at(i);
    if (!crash_recovery::recovery_tick_should_run(doc.dirty(), live_effect_preview_, stroke, text,
                                                 floating)) {
      continue;
    }
    const bool large = doc.width() > 2048 || doc.height() > 2048;
    OraSnapshot snapshot = capture_ora_snapshot(doc, !large || idle_long);
    if (i == active && effect_preview_ != nullptr && effect_preview_->previewing()) {
      const Layer* pristine = effect_preview_->pristine();
      const int layer_index = effect_preview_->layer_index();
      if (pristine != nullptr && layer_index >= 0 &&
          layer_index < static_cast<int>(snapshot.layers.size())) {
        OraSnapshotLayer& item = snapshot.layers[static_cast<std::size_t>(layer_index)];
        const std::size_t bytes = static_cast<std::size_t>(pristine->stride()) *
                                  static_cast<std::size_t>(pristine->height());
        item.pixels.assign(pristine->pixels(), pristine->pixels() + bytes);
        item.width = pristine->width();
        item.height = pristine->height();
        item.stride = pristine->stride();
        item.revision += 1;
      }
    }
    if (i == active && active_tool_ != nullptr) {
      int overlay_index = active_tool_->preview_layer();
      if (overlay_index < 0) {
        overlay_index = doc.layers().active_index();
      }
      if (overlay_index >= 0 && overlay_index < static_cast<int>(snapshot.layers.size())) {
        OraSnapshotLayer& item = snapshot.layers[static_cast<std::size_t>(overlay_index)];
        if (active_tool_->paint_recovery_overlay(overlay_index, item.pixels.data(), item.width,
                                                item.height, item.stride)) {
          item.revision += 1;
        }
      }
    }
    if (doc.selection().floating()) {
      int float_index = doc.selection().source_layer();
      if (float_index < 0 || float_index >= static_cast<int>(snapshot.layers.size())) {
        float_index = doc.layers().active_index();
      }
      if (float_index >= 0 && float_index < static_cast<int>(snapshot.layers.size())) {
        OraSnapshotLayer& item = snapshot.layers[static_cast<std::size_t>(float_index)];
        const Layer& layer = doc.layers().at(float_index);
        if (composite_floating_into_buffer(doc.selection(), item.pixels.data(), item.width,
                                          item.height, item.stride, layer.offset_x(),
                                          layer.offset_y())) {
          item.revision += 1;
        }
      }
    }
    jobs.push_back(Job{std::move(snapshot), crash_recovery::autosave_path_for(doc.recovery_id())});
  }
  if (jobs.empty()) {
    return;
  }
  {
    std::lock_guard<std::mutex> lock(recovery_slot_->mu);
    if (recovery_slot_->window == nullptr) {
      return;
    }
    if (recovery_slot_->busy) {
      recovery_slot_->pending = true;
      return;
    }
    recovery_slot_->busy = true;
  }
  auto slot = recovery_slot_;
  try {
    std::thread([slot, jobs = std::move(jobs)]() mutable {
      try {
        for (Job& job : jobs) {
          std::string error;
          if (!crash_recovery::prepare_state_dir(error) ||
              !save_ora_snapshot(job.dest, job.snapshot, error)) {
            // Leave any previous recovery file in place.
          }
        }
      } catch (...) {
      }
      g_idle_add(&MainWindow::recovery_idle_cb, new RecoveryIdle{slot});
    }).detach();
  } catch (...) {
    std::lock_guard<std::mutex> lock(recovery_slot_->mu);
    recovery_slot_->busy = false;
  }
}

bool MainWindow::on_recovery_tick() {
  const bool stroke = active_tool_ != nullptr && active_tool_->is_stroking();
  const bool text = active_tool_ != nullptr && active_tool_->captures_keys();
  const bool floating = document_ptr() != nullptr && document().selection().floating();
  bool any = false;
  for (int i = 0; i < workspace_.count(); ++i) {
    if (crash_recovery::recovery_tick_should_run(workspace_.at(i).dirty(), live_effect_preview_,
                                                stroke, text, floating)) {
      any = true;
      break;
    }
  }
  if (!any) {
    return true;
  }
  if (recovery_slot_) {
    std::lock_guard<std::mutex> lock(recovery_slot_->mu);
    if (recovery_slot_->busy) {
      recovery_slot_->pending = true;
      return true;
    }
  }
  start_recovery_save();
  return true;
}

void MainWindow::on_drag_data_received(const Glib::RefPtr<Gdk::DragContext>& context, int /*x*/,
                                      int /*y*/, const Gtk::SelectionData& data, guint /*info*/,
                                      guint time) {
  bool ok = false;
  for (const auto& uri : data.get_uris()) {
    auto file = Gio::File::create_for_uri(uri);
    if (!file) {
      continue;
    }
    const std::string path = file->get_path();
    if (!path.empty() && open_path(path)) {
      ok = true;
    }
  }
  context->drag_finish(ok, false, time);
}

}  // namespace lundukepaint
