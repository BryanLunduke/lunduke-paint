// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_LAYERS_PANEL_HPP
#define LUNDUKEPAINT_UI_LAYERS_PANEL_HPP

#include <string>

#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>

namespace lundukepaint {

class Document;

class LayersPanel : public Gtk::Box {
public:
  LayersPanel();

  void set_document(Document* document);
  void refresh();
  void show_properties();

private:
  void add_row(int stack_index);
  void on_row_selected(Gtk::ListBoxRow* row);
  void popup_row_menu(int stack_index, GdkEventButton* event);
  void rename_layer(int stack_index);
  void add_layer_clicked();
  void delete_layer_clicked();
  void rename_clicked();
  int selected_stack_index() const;
  void update_buttons();
  void style_icon_button(Gtk::Button& button, const std::string& icon_name, const char* tip);
  bool on_list_drag_motion(const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, guint time);
  void on_list_drag_leave(const Glib::RefPtr<Gdk::DragContext>& context, guint time);
  bool on_list_drag_drop(const Glib::RefPtr<Gdk::DragContext>& context, int x, int y, guint time);
  void on_list_drag_data_received(const Glib::RefPtr<Gdk::DragContext>& context, int x, int y,
                                  const Gtk::SelectionData& data, guint info, guint time);
  void on_row_drag_begin(const Glib::RefPtr<Gdk::DragContext>& context, Gtk::ListBoxRow* row);
  void on_row_drag_data_get(const Glib::RefPtr<Gdk::DragContext>& context,
                            Gtk::SelectionData& selection_data, guint info, guint time,
                            int stack_index);

  Document* document_{nullptr};
  bool refreshing_{false};
  int drag_stack_from_{-1};

  Gtk::ScrolledWindow scroll_;
  Gtk::ListBox list_;
  Gtk::Box toolbar_{Gtk::ORIENTATION_HORIZONTAL, 2};
  Gtk::Button del_;
  Gtk::Button rename_;
  Gtk::Button add_;
};

}  // namespace lundukepaint

#endif
