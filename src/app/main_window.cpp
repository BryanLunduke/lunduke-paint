// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/main_window.hpp"

#include "app/actions.hpp"
#include "doc/commands_image.hpp"
#include "doc/commands_layers.hpp"
#include "doc/commands_pixels.hpp"
#include "doc/effect_preview.hpp"
#include "doc/selection.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"
#include "io/crash_recovery.hpp"
#include "raster/effects.hpp"
#include "raster/transform.hpp"
#include "ui/dialogs_adjust.hpp"
#include "ui/dialogs_image.hpp"
#include "ui/dialogs_new.hpp"
#include "ui/dialogs_prefs.hpp"
#include "ui/intro_howdy.hpp"
#include "tools/tools.hpp"

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

MainWindow::MainWindow() {
  prefs_.load();
  auto startup = Document::create(prefs_.default_width, prefs_.default_height, Color::white());
  startup->history().set_depth(prefs_.undo_limit);
  workspace_.add(std::move(startup));
  set_title(Glib::ustring("Untitled — ") + actions::kProductName);
  set_default_size(1100, 720);
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

  tools_.emplace_back(create_rect_select_tool());
  tools_.emplace_back(create_lasso_tool());
  tools_.emplace_back(create_ellipse_select_tool());
  tools_.emplace_back(create_magic_wand_tool());
  tools_.emplace_back(create_pencil_tool());
  tools_.emplace_back(create_brush_tool());
  tools_.emplace_back(create_eraser_tool());
  tools_.emplace_back(create_fill_tool());
  tools_.emplace_back(create_picker_tool());
  tools_.emplace_back(create_line_tool());
  tools_.emplace_back(create_rectangle_tool());
  tools_.emplace_back(create_ellipse_tool());
  tools_.emplace_back(create_color_eraser_tool());
  tools_.emplace_back(create_spray_tool());
  tools_.emplace_back(create_rounded_rect_tool());
  tools_.emplace_back(create_polyline_tool());
  tools_.emplace_back(create_polygon_tool());
  tools_.emplace_back(create_curve_tool());
  tools_.emplace_back(create_text_tool());
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
  // Without this the Size spin button in the tool options bar owns the initial
  // keyboard focus, focus_is_editable() is true and every single-letter tool
  // shortcut is eaten by that entry (so the toolbox highlight never moved).
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
  recovery_timer_.disconnect();
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

  toolbox_.add_tool_button("rect-select", "Rectangle select (S)", "tool-select-rectangle-symbolic");
  toolbox_.add_tool_button("lasso", "Freeform select (M)", "tool-select-lasso-freeform-symbolic");
  toolbox_.add_tool_button("ellipse-select", "Ellipse select (I)", "tool-select-ellipse-symbolic");
  toolbox_.add_tool_button("magic-wand", "Magic wand (W)", "tool-magicwand-symbolic");
  toolbox_.add_tool_button("pencil", "Pencil (P)", "tool-pencil-symbolic");
  toolbox_.add_tool_button("brush", "Brush (B)", "tool-paintbrush-symbolic");
  toolbox_.add_tool_button("eraser", "Eraser (A)", "tool-eraser-symbolic");
  toolbox_.add_tool_button("fill", "Flood fill (F)", "tool-paintbucket-symbolic");
  toolbox_.add_tool_button("picker", "Color picker (C)", "tool-colorpicker-symbolic");
  toolbox_.add_tool_button("line", "Line (L)", "tool-line-symbolic");
  toolbox_.add_tool_button("rectangle", "Rectangle (R)", "tool-rectangle-symbolic");
  toolbox_.add_tool_button("ellipse", "Ellipse (E)", "tool-ellipse-symbolic");
  toolbox_.add_tool_button("color-eraser", "Color eraser (O)", "tool-recolor-symbolic");
  toolbox_.add_tool_button("spray", "Spraycan (Y)", "tool-spray-symbolic");
  toolbox_.add_tool_button("rounded-rect", "Rounded rectangle (U)", "tool-rectangle-rounded-symbolic");
  toolbox_.add_tool_button("polyline", "Polyline (N)", "tool-polyline-symbolic");
  toolbox_.add_tool_button("polygon", "Polygon (G)", "tool-select-lasso-polygon-symbolic");
  toolbox_.add_tool_button("curve", "Curve (V)", "tool-curve-symbolic");
  toolbox_.add_tool_button("text", "Text (T)", "tool-text-symbolic");
  toolbox_.on_tool_chosen = [this](const std::string& id) {
    set_active_tool(id);
    // Clicking a tool should leave the keyboard on the canvas, so the letter
    // shortcuts keep working afterwards.
    canvas_.focus_canvas();
  };
  toolbox_.on_well_clicked = [this](bool background) { choose_color(background); };
  toolbox_.on_transparent = [this](bool background) {
    if (background) {
      document().set_background(Color::transparent());
    } else {
      document().set_foreground(Color::transparent());
    }
  };
  colors_panel_.on_swatch = [this](Color color, bool background) {
    if (background) {
      document().set_background(color);
    } else {
      document().set_foreground(color);
    }
  };

  auto* layers_tab = Gtk::make_managed<Gtk::Label>("Layers");
  auto* history_tab = Gtk::make_managed<Gtk::Label>("History");
  right_dock_.append_page(layers_panel_, *layers_tab);
  right_dock_.append_page(history_panel_, *history_tab);
  right_dock_.set_scrollable(false);
  right_dock_.set_hexpand(true);
  right_dock_.set_vexpand(true);

  colors_panel_.set_hexpand(true);
  colors_panel_.set_valign(Gtk::ALIGN_END);

  // Wide enough for the "Layers" and "History" tab labels, and no wider.
  constexpr int kRightDockWidth = 240;
  right_sidebar_.set_size_request(kRightDockWidth, -1);
  right_dock_.set_size_request(kRightDockWidth, -1);
  colors_panel_.set_size_request(kRightDockWidth, -1);

  right_sidebar_.set_spacing(0);
  right_sidebar_.set_hexpand(false);
  right_sidebar_.set_halign(Gtk::ALIGN_FILL);
  right_sidebar_.pack_start(right_dock_, Gtk::PACK_EXPAND_WIDGET);
  right_sidebar_.pack_start(colors_panel_, Gtk::PACK_SHRINK);

  auto* left_sep = Gtk::make_managed<Gtk::Separator>(Gtk::ORIENTATION_VERTICAL);

  toolbox_.attach_tool_options(tool_options_bar_);
  work_area_.pack_start(toolbox_, Gtk::PACK_SHRINK);
  work_area_.pack_start(*left_sep, Gtk::PACK_SHRINK);
  work_area_.pack_start(canvas_, Gtk::PACK_EXPAND_WIDGET);
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
    if (active_tool_ != nullptr) {
      active_tool_->on_cancel();
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
  toolbar_.pack_start(*toolbar_button("view-sidebar-end-symbolic", "Right dock (F12)",
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
  history_panel_.on_jump = [this](int index) {
    if (active_tool_ != nullptr) {
      active_tool_->on_cancel();
    }
    document().jump_history(index);
  };
  document().set_on_changed([this]() { update_chrome(); });
  document().set_on_invalidated([this](Rect rect) { canvas_.invalidate_rect(rect); });
  toolbox_.set_colors(document().foreground(), document().background());
  update_chrome();
}

void MainWindow::reset_canvas() {
  new_document(kDefaultWidth, kDefaultHeight, Color::white());
}

void MainWindow::new_document(int width, int height, Color background) {
  adopt_document(Document::create(width, height, background), false);
}

void MainWindow::adopt_document(std::unique_ptr<Document> document, bool prefer_replace) {
  if (!document) {
    return;
  }
  document->history().set_depth(prefs_.undo_limit);
  if (active_tool_ != nullptr) {
    active_tool_->on_cancel();
  }
  detach_document();
  if (prefer_replace && workspace_.count() == 1 && workspace_.is_placeholder(0)) {
    workspace_.replace_active(std::move(document));
  } else {
    workspace_.add(std::move(document));
  }
  attach_active_document();
  rebuild_tabs();
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
      tool_options_bar_.show_tool(found);
    }
    return;
  }
  if (active_tool_ != nullptr) {
    if (active_tool_->captures_keys()) {
      active_tool_->on_commit();
    }
    active_tool_->on_cancel();
    previous_tool_ = active_tool_;
  }
  active_tool_ = found;
  canvas_.set_tool(active_tool_);
  toolbox_.set_active_tool(id);
  tool_options_bar_.show_tool(active_tool_);
  status_bar_.set_hint(active_tool_->hint());
}

void MainWindow::set_stroke_size(int size) {
  if (size < 1) {
    size = 1;
  }
  stroke_size_ = size;
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
  if (active_tool_ != nullptr) {
    active_tool_->on_cancel();
  }
  document().undo();
}

void MainWindow::on_redo() {
  if (active_tool_ != nullptr) {
    active_tool_->on_cancel();
  }
  document().redo();
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
  }
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
  if (event->keyval == GDK_KEY_space) {
    canvas_.set_space_down(true);
    return false;
  }
  if (focus_is_editable()) {
    return false;
  }
  if (event->keyval == GDK_KEY_Escape) {
    if (active_tool_ != nullptr && (active_tool_->is_stroking() || active_tool_->captures_keys())) {
      active_tool_->on_cancel();
      return true;
    }
    action_deselect();
    return true;
  }
  if (event->keyval == GDK_KEY_Return || event->keyval == GDK_KEY_KP_Enter) {
    if (active_tool_ != nullptr && active_tool_->on_commit()) {
      return true;
    }
  }
  if (active_tool_ != nullptr && active_tool_->captures_keys()) {
    return false;
  }
  if ((event->state & (GDK_CONTROL_MASK | GDK_MOD1_MASK)) != 0) {
    return false;
  }
  const guint32 ch = gdk_keyval_to_unicode(gdk_keyval_to_upper(event->keyval));
  if (ch == 'X') {
    document().swap_colors();
    return true;
  }
  if (ch == 'D') {
    document().reset_colors();
    return true;
  }
  for (auto& tool : tools_) {
    if (tool->shortcut() == static_cast<char>(ch)) {
      set_active_tool(tool->id());
      return true;
    }
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
  if (active_tool_ != nullptr) {
    active_tool_->on_cancel();
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
    } else {
      adopt_document(std::move(doc), true);
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
  } else {
    adopt_document(std::move(doc), true);
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
    crash_recovery::clear();
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
    if (also_ora) {
      std::string ora_path = path;
      auto dot = ora_path.find_last_of('.');
      if (dot != std::string::npos) {
        ora_path = ora_path.substr(0, dot);
      }
      ora_path += ".ora";
      std::string error;
      if (!save_ora(ora_path, document(), error)) {
        Gtk::MessageDialog err(*this, "Saved the flat file, but could not write the .ora copy.",
                               false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK, true);
        err.set_secondary_text(error);
        err.run();
      }
    }
    document().set_path(path);
    document().mark_clean();
    remember_recent(path);
    crash_recovery::clear();
    update_chrome();
    show_status("Saved");
  }
}

bool MainWindow::confirm_close() {
  for (int i = 0; i < workspace_.count(); ++i) {
    workspace_.set_active(i);
    attach_active_document();
    if (!confirm_lose_document(workspace_.at(i))) {
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
  if (!document.dirty()) {
    return true;
  }
  Glib::ustring name = document.path().empty() ? "Untitled" : Glib::path_get_basename(document.path());
  Gtk::MessageDialog dialog(*this, "Save changes to " + name + "?", false,
                            Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Discard", Gtk::RESPONSE_NO);
  dialog.add_button("_Save", Gtk::RESPONSE_YES);
  const int response = dialog.run();
  if (response == Gtk::RESPONSE_CANCEL) {
    return false;
  }
  if (response == Gtk::RESPONSE_YES) {
    const int idx = workspace_.index_of(&document);
    if (idx >= 0) {
      workspace_.set_active(idx);
      attach_active_document();
    }
    action_save();
    return !document.dirty();
  }
  return true;
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

bool MainWindow::save_to_path(const std::string& path, ImageFormat format) {
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

  const bool multi = document().layers().count() > 1;
  if (multi && format == ImageFormat::Png) {
    Gtk::MessageDialog warn(*this,
                            "PNG will flatten visible layers (alpha is kept).",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
    if (warn.run() != Gtk::RESPONSE_OK) {
      return false;
    }
  }
  if (format == ImageFormat::Jpeg && (layer_has_transparency() || multi)) {
    Gtk::MessageDialog warn(*this,
                            "JPEG cannot store transparency or layers. The image will be flattened onto white.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
    if (warn.run() != Gtk::RESPONSE_OK) {
      return false;
    }
  }
  if (format == ImageFormat::Bmp && (layer_has_transparency() || multi)) {
    Gtk::MessageDialog warn(*this,
                            "BMP cannot store transparency or layers. The image will be flattened onto white.",
                            false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
    if (warn.run() != Gtk::RESPONSE_OK) {
      return false;
    }
  }
  std::vector<std::uint8_t> flat;
  composite_visible(flat);
  std::string error;
  if (format == ImageFormat::Gif) {
    Gtk::MessageDialog err(*this, "GIF save is not supported.", false, Gtk::MESSAGE_ERROR,
                           Gtk::BUTTONS_OK, true);
    err.run();
    return false;
  }
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

std::string MainWindow::choose_open_path() {
  Gtk::FileChooserDialog dialog(*this, "Open Image", Gtk::FILE_CHOOSER_ACTION_OPEN);
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Open", Gtk::RESPONSE_ACCEPT);
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
    path += format_extension(format);
  } else if (forced != ImageFormat::Unknown && forced != format) {
    format = forced;
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
      path = path.substr(0, dot);
    }
    path += format_extension(format);
  }
  // Never write a flat image over an existing .ora path.
  if (format != ImageFormat::Ora && format_from_path(path) == ImageFormat::Ora) {
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
      path = path.substr(0, dot);
    }
    path += format_extension(format);
  }
  if (format != ImageFormat::Ora && !document().path().empty() &&
      format_from_path(document().path()) == ImageFormat::Ora && path == document().path()) {
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
      path = path.substr(0, dot);
    }
    path += format_extension(format);
  }
  return true;
}


void MainWindow::copy_selection_to_clipboard() {
  if (document().selection().empty()) {
    return;
  }
  int w = 0;
  int h = 0;
  std::vector<std::uint8_t> rgba;
  copy_selection_rgba(document().layers().active_layer(), document().selection(), document().width(),
                      document().height(), w, h, rgba);
  if (w < 1 || h < 1 || rgba.empty()) {
    return;
  }
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
  copy_merged_rgba(document().layers(), document().selection(), document().width(),
                   document().height(), w, h, rgba);
  if (w < 1 || h < 1 || rgba.empty()) {
    return;
  }
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
  auto pixbuf = Gtk::Clipboard::get()->wait_for_image();
  if (!pixbuf) {
    show_status("Clipboard has no image");
    return false;
  }
  const int w = pixbuf->get_width();
  const int h = pixbuf->get_height();
  if (w < 1 || h < 1) {
    return false;
  }
  auto rgba_buf = pixbuf->add_alpha(false, 0, 0, 0);
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
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
  int px = 0;
  int py = 0;
  if (!canvas_.last_pointer(px, py)) {
    canvas_.viewport_center_canvas(px, py);
  }
  document().paste_floating(px, py, w, h, std::move(rgba));
  set_active_tool("rect-select");
  show_status("Pasted");
  return true;
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
  if (active_tool_ != nullptr && active_tool_->is_stroking()) {
    active_tool_->on_cancel();
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

void MainWindow::commit_stack_transform(
    const char* name, int new_w, int new_h,
    const std::function<void(const Layer&, std::vector<std::uint8_t>&, int, int)>& xform) {
  auto old_layers = document().snapshot_layers();
  std::vector<LayerSnapshot> new_layers;
  new_layers.reserve(old_layers.size());
  for (int i = 0; i < document().layers().count(); ++i) {
    const Layer& layer = document().layers().at(i);
    std::vector<std::uint8_t> dest(static_cast<std::size_t>(new_w) * static_cast<std::size_t>(new_h) *
                                   4);
    xform(layer, dest, new_w, new_h);
    LayerSnapshot snap = snapshot_layer_props(layer);
    snap.width = new_w;
    snap.height = new_h;
    snap.offset_x = 0;
    snap.offset_y = 0;
    snap.pixels = std::move(dest);
    new_layers.push_back(std::move(snap));
  }
  auto cmd = std::make_unique<AllLayersBufferCommand>(
      name, document().width(), document().height(), document().layers().active_index(),
      std::move(old_layers), new_w, new_h, document().layers().active_index(),
      std::move(new_layers));
  document().commit(std::move(cmd));
  canvas_.refresh_size();
  canvas_.invalidate_all();
}

void MainWindow::action_canvas_size() {
  document().commit_floating();
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
  commit_stack_transform("Canvas size", nw, nh, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                                    int dw, int dh) {
    resize_canvas(layer.pixels(), layer.width(), layer.height(), layer.stride(), dest.data(), dw, dh,
                  dw * 4, fill);
  });
}

void MainWindow::action_scale() {
  document().commit_floating();
  ScaleImageDialog dialog(*this, document().width(), document().height());
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  const int nw = dialog.image_width();
  const int nh = dialog.image_height();
  if (!warn_size(nw, nh)) {
    return;
  }
  const bool nearest = dialog.nearest();
  commit_stack_transform("Scale", nw, nh, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                              int dw, int dh) {
    if (nearest) {
      scale_nearest(layer.pixels(), layer.width(), layer.height(), layer.stride(), dest.data(), dw, dh,
                    dw * 4);
    } else {
      scale_bilinear(layer.pixels(), layer.width(), layer.height(), layer.stride(), dest.data(), dw,
                     dh, dw * 4);
    }
  });
}

void MainWindow::action_crop() {
  document().commit_floating();
  const Selection& sel = document().selection();
  if (sel.empty() || sel.inverted()) {
    return;
  }
  Rect r = rect_intersect(sel.bounds(), Rect{0, 0, document().width(), document().height()});
  if (r.empty()) {
    return;
  }
  commit_stack_transform("Crop", r.w, r.h, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                               int dw, int dh) {
    crop_rect(layer.pixels(), layer.width(), layer.height(), layer.stride(), r, dest.data(), dw * 4);
    (void)dh;
  });
}

void MainWindow::action_autocrop() {
  document().commit_floating();
  const Layer& layer = document().layers().active_layer();
  const Rect r = autocrop_bounds(layer.pixels(), layer.width(), layer.height(), layer.stride());
  if (r.empty() || (r.x == 0 && r.y == 0 && r.w == layer.width() && r.h == layer.height())) {
    show_status("Nothing to autocrop");
    return;
  }
  commit_stack_transform("Autocrop", r.w, r.h, [&](const Layer& L, std::vector<std::uint8_t>& dest,
                                                   int dw, int dh) {
    crop_rect(L.pixels(), L.width(), L.height(), L.stride(), r, dest.data(), dw * 4);
    (void)dh;
  });
}

void MainWindow::action_rotate_90() {
  document().commit_floating();
  const int nw = document().height();
  const int nh = document().width();
  commit_stack_transform("Rotate 90", nw, nh, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                                  int dw, int dh) {
    rotate_90_cw(layer.pixels(), layer.width(), layer.height(), layer.stride(), dest.data(), dw * 4);
    (void)dh;
  });
}

void MainWindow::action_rotate_180() {
  document().commit_floating();
  const int w = document().width();
  const int h = document().height();
  commit_stack_transform("Rotate 180", w, h, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                                 int dw, int dh) {
    dest = copy_layer_pixels(layer);
    rotate_180(dest.data(), layer.width(), layer.height(), layer.width() * 4);
    (void)dw;
    (void)dh;
  });
}

void MainWindow::action_rotate_ccw() {
  document().commit_floating();
  const int nw = document().height();
  const int nh = document().width();
  commit_stack_transform("Rotate 270", nw, nh, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                                   int dw, int dh) {
    rotate_90_ccw(layer.pixels(), layer.width(), layer.height(), layer.stride(), dest.data(), dw * 4);
    (void)dh;
  });
}

void MainWindow::action_flip_h() {
  document().commit_floating();
  const int w = document().width();
  const int h = document().height();
  commit_stack_transform("Flip horizontal", w, h, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                                     int dw, int dh) {
    dest = copy_layer_pixels(layer);
    flip_h(dest.data(), layer.width(), layer.height(), layer.width() * 4);
    (void)dw;
    (void)dh;
  });
}

void MainWindow::action_flip_v() {
  document().commit_floating();
  const int w = document().width();
  const int h = document().height();
  commit_stack_transform("Flip vertical", w, h, [&](const Layer& layer, std::vector<std::uint8_t>& dest,
                                                   int dw, int dh) {
    dest = copy_layer_pixels(layer);
    flip_v(dest.data(), layer.width(), layer.height(), layer.width() * 4);
    (void)dw;
    (void)dh;
  });
}

void MainWindow::action_clear() {
  if (document().active_locked()) {
    show_status("Layer is locked");
    return;
  }
  document().commit_floating();
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
  document().add_layer();
  right_dock_.set_current_page(0);
  show_status("Added layer");
}

void MainWindow::action_layer_duplicate() {
  document().duplicate_layer();
  show_status("Duplicated layer");
}

void MainWindow::action_layer_delete() {
  if (!document().delete_layer()) {
    show_status("Cannot delete the last layer");
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
  if (!document().merge_down()) {
    show_status("Nothing below to merge");
    return;
  }
  show_status("Merged down");
}

void MainWindow::action_layer_flatten() {
  if (document().layers().count() <= 1) {
    return;
  }
  document().flatten();
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
  workspace_.set_active(index);
  attach_active_document();
  if (!confirm_lose_document(workspace_.at(index))) {
    return false;
  }
  if (active_tool_ != nullptr) {
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
    show_status("Layer is locked");
    return;
  }
  document().commit_floating();
  EffectPreview effect(document());
  if (!effect.valid()) {
    return;
  }
  effect.commit(name != nullptr ? name : "Adjust", fn);
}

bool MainWindow::run_adjust_dialog(LivePreviewDialog& dialog, const char* name,
                                   const std::function<EffectPreview::EffectFn()>& build_effect) {
  if (document().active_locked()) {
    show_status("Layer is locked");
    return false;
  }
  document().commit_floating();
  EffectPreview effect(document());
  if (!effect.valid()) {
    return false;
  }
  // Live preview repaints the layer in place, straight from the snapshot, so
  // dragging never stacks and never pushes history.
  dialog.on_preview = [this, &effect, &build_effect]() {
    const EffectPreview::EffectFn fn = build_effect();
    if (fn) {
      effect.preview(fn);
    } else {
      effect.restore();
    }
    canvas_.invalidate_all();
  };
  dialog.on_preview_reset = [this, &effect]() {
    effect.restore();
    canvas_.invalidate_all();
  };

  const int response = dialog.run();
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
  run_adjust_dialog(dialog, "Blur", [&dialog]() -> EffectPreview::EffectFn {
    const int radius = dialog.radius();
    return [radius](std::uint8_t* px, int w, int h, int stride) {
      std::vector<std::uint8_t> src(static_cast<std::size_t>(h) * static_cast<std::size_t>(stride));
      std::memcpy(src.data(), px, src.size());
      box_blur_rgba(src.data(), w, h, stride, px, stride, radius);
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
  const char* rows[][2] = {
      {"New / Open / Save / Save As", "Ctrl+N / O / S / Shift+S"},
      {"Close / Quit", "Ctrl+W / Q"},
      {"Print", "Ctrl+P"},
      {"Undo", "Ctrl+Z"},
      {"Redo", "Ctrl+Y or Ctrl+Shift+Z"},
      {"Cut / Copy / Paste / Select all", "Ctrl+X / C / V / A"},
      {"Deselect", "Ctrl+D or Esc"},
      {"Delete selection", "Delete"},
      {"Duplicate selection", "Ctrl+J"},
      {"New layer", "Ctrl+Shift+N"},
      {"Merge down", "Ctrl+E"},
      {"Flatten", "Ctrl+Shift+E"},
      {"Zoom in / out / 100% / fit", "Ctrl++ / Ctrl+- / Ctrl+0 / Ctrl+1"},
      {"Grid / dock / fullscreen", "Ctrl+G / F12 / F11"},
      {"Swap FG-BG / default colors", "X / D"},
      {"Pencil / Brush / Eraser", "P / B / A"},
      {"Rectangle select / Lasso / Ellipse select", "S / M / I"},
      {"Magic wand / Fill / Picker", "W / F / C"},
      {"Line / Rectangle / Ellipse", "L / R / E"},
      {"Text / Curve / Polygon", "T / V / G"},
      {"Spray / Rounded rect / Polyline", "Y / U / N"},
      {"Color eraser", "O"},
  };
  const int nrows = static_cast<int>(sizeof(rows) / sizeof(rows[0]));
  for (int i = 0; i < nrows; ++i) {
    auto* action = Gtk::manage(new Gtk::Label(rows[i][0], Gtk::ALIGN_START));
    auto* keys = Gtk::manage(new Gtk::Label(rows[i][1], Gtk::ALIGN_START));
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
  dialog.set_comments("A traditional X11 paint program.\nApplication id: " +
                      Glib::ustring(actions::kAppId));
  dialog.set_copyright("Copyright © 2026 The Lunduke Journal");
  dialog.set_license_type(Gtk::LICENSE_GPL_3_0);
  dialog.set_wrap_license(true);
  dialog.set_website("https://lunduke.com");
  dialog.set_website_label("lunduke.com");
  dialog.set_logo_icon_name(actions::kAppId);
  dialog.run();
}

void MainWindow::action_print() {
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
  if (document().dirty()) {
    Gtk::MessageDialog dialog(*this, "Revert to last saved file? Unsaved changes will be lost.",
                              false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
    dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
    dialog.add_button("_Revert", Gtk::RESPONSE_YES);
    if (dialog.run() != Gtk::RESPONSE_YES) {
      return;
    }
  }
  if (active_tool_ != nullptr) {
    active_tool_->on_cancel();
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
  if (path == crash_recovery::autosave_path()) {
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
      auto* item = Gtk::manage(new Gtk::MenuItem(path));
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
  using lundukepaint::crash_recovery::autosave_path;
  using lundukepaint::crash_recovery::exists;
  if (!exists()) {
    return;
  }
  Gtk::MessageDialog dialog(*this, "Recover unsaved document?", false, Gtk::MESSAGE_QUESTION,
                            Gtk::BUTTONS_NONE, true);
  dialog.set_secondary_text("A crash-recovery OpenRaster file was found from a previous session.");
  dialog.add_button("_Discard", Gtk::RESPONSE_NO);
  dialog.add_button("_Recover", Gtk::RESPONSE_YES);
  const int response = dialog.run();
  if (response == Gtk::RESPONSE_YES) {
    open_path(autosave_path());
    if (document_ptr() != nullptr) {
      document().set_path(std::string());
      document().set_dirty(true);
    }
  } else {
    crash_recovery::clear();
  }
}

bool MainWindow::on_recovery_tick() {
  if (document_ptr() == nullptr || !document().dirty()) {
    return true;
  }
  std::string error;
  crash_recovery::write_document(document(), error);
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
