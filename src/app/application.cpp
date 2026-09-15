// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/application.hpp"

#include "app/actions.hpp"
#include "app/main_window.hpp"

#include <giomm/application.h>
#include <gtkmm/icontheme.h>
#include <gtkmm/window.h>

namespace lundukepaint {

Glib::RefPtr<Application> Application::create() {
  return Glib::RefPtr<Application>(new Application());
}

Application::Application()
    : Gtk::Application(actions::kAppId, Gio::APPLICATION_HANDLES_OPEN) {}

void Application::on_startup() {
  Gtk::Application::on_startup();
  // WM / title-bar icon (xfwm4 etc.): desktop Icon= alone is not enough.
  Gtk::Window::set_default_icon_name(actions::kAppId);
  Gtk::IconTheme::get_default()->add_resource_path("/org/lunduke/LundukePaint/icons");

  add_action(actions::kNew, sigc::mem_fun(*this, &Application::on_action_new));
  add_action(actions::kOpen, sigc::mem_fun(*this, &Application::on_action_open));
  add_action(actions::kSave, sigc::mem_fun(*this, &Application::on_action_save));
  add_action(actions::kSaveAs, sigc::mem_fun(*this, &Application::on_action_save_as));
  add_action(actions::kQuit, sigc::mem_fun(*this, &Application::on_action_quit));

  set_accels_for_action("app.new", {"<Primary>n"});
  set_accels_for_action("app.open", {"<Primary>o"});
  set_accels_for_action("app.save", {"<Primary>s"});
  set_accels_for_action("app.save-as", {"<Primary><Shift>s"});
  set_accels_for_action("app.quit", {"<Primary>q"});
  set_accels_for_action("win.close-tab", {"<Primary>w"});
  set_accels_for_action("win.toggle-right-dock", {"F12"});
  set_accels_for_action("win.fullscreen", {"F11"});
  set_accels_for_action("win.undo", {"<Primary>z"});
  set_accels_for_action("win.redo", {"<Primary>y", "<Primary><Shift>z"});
  set_accels_for_action("win.zoom-in", {"<Primary>plus", "<Primary>equal"});
  set_accels_for_action("win.zoom-out", {"<Primary>minus"});
  set_accels_for_action("win.zoom-100", {"<Primary>0"});
  set_accels_for_action("win.zoom-fit", {"<Primary>1"});
  set_accels_for_action("win.toggle-grid", {"<Primary>g"});
  set_accels_for_action("win.cut", {"<Primary>x"});
  set_accels_for_action("win.copy", {"<Primary>c"});
  set_accels_for_action("win.copy-merged", {"<Primary><Shift>c"});
  set_accels_for_action("win.paste", {"<Primary>v"});
  set_accels_for_action("win.delete", {"Delete", "BackSpace"});
  set_accels_for_action("win.duplicate", {"<Primary>j"});
  set_accels_for_action("win.select-all", {"<Primary>a"});
  set_accels_for_action("win.deselect", {"<Primary>d"});
  set_accels_for_action("win.layer-new", {"<Primary><Shift>n"});
  set_accels_for_action("win.layer-merge-down", {"<Primary>e"});
  set_accels_for_action("win.layer-flatten", {"<Primary><Shift>e"});
  set_accels_for_action("win.print", {"<Primary>p"});
  set_accels_for_action("win.revert", {"<Primary>r"});
}

bool Application::ensure_window() {
  if (window_ != nullptr) {
    return false;
  }
  window_ = new MainWindow();
  add_window(*window_);
  window_->signal_hide().connect([this]() {
    MainWindow* dying = window_;
    window_ = nullptr;
    delete dying;
  });
  return true;
}

void Application::on_activate() {
  const bool created = ensure_window();
  window_->present();
  if (created) {
    // Fresh empty launch: play_intro() is a no-op unless LUNDUKEPAINT_HOWDY_INTRO
    // is enabled. A second activation that only re-presents must not replay.
    window_->play_intro();
  }
}

void Application::on_open(const Gio::Application::type_vec_files& files, const Glib::ustring& /*hint*/) {
  // No greeting on this path: the user asked for a file, not an empty canvas.
  ensure_window();
  window_->present();
  for (const auto& file : files) {
    if (!file) {
      continue;
    }
    const std::string path = file->get_path();
    if (!path.empty()) {
      window_->open_path(path);
    }
  }
}

void Application::on_action_new() {
  if (window_ != nullptr) {
    window_->action_new();
  }
}

void Application::on_action_open() {
  if (window_ != nullptr) {
    window_->action_open();
  }
}

void Application::on_action_save() {
  if (window_ != nullptr) {
    window_->action_save();
  }
}

void Application::on_action_save_as() {
  if (window_ != nullptr) {
    window_->action_save_as();
  }
}

void Application::on_action_quit() {
  if (window_ != nullptr && !window_->confirm_close()) {
    return;
  }
  quit();
}

}  // namespace lundukepaint
