// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_HISTORY_PANEL_HPP
#define LUNDUKEPAINT_UI_HISTORY_PANEL_HPP

#include <functional>
#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>

namespace lundukepaint {

class Document;

class HistoryPanel : public Gtk::Box {
public:
  HistoryPanel();

  void set_document(Document* document);
  void refresh();

  // Called with the history index to jump to (-1 = initial document).
  std::function<void(int index)> on_jump;

private:
  void on_row_activated(Gtk::ListBoxRow* row);

  Document* document_{nullptr};
  bool refreshing_{false};

  Gtk::Label notice_;
  Gtk::ScrolledWindow scroll_;
  Gtk::ListBox list_;
};

}  // namespace lundukepaint

#endif
