// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/layers_panel.hpp"

#include "doc/document.hpp"
#include "raster/blend.hpp"

#include <gdkmm/pixbuf.h>
#include <gtkmm/comboboxtext.h>
#include <gtkmm/entry.h>
#include <gtkmm/image.h>
#include <gtkmm/label.h>
#include <gtkmm/menu.h>
#include <gtkmm/menuitem.h>
#include <gtkmm/messagedialog.h>
#include <gtkmm/separatormenuitem.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/targetentry.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/window.h>

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <gtk/gtk.h>

namespace lundukepaint {
namespace {

constexpr const char* kLayerDnD = "application/x-lunduke-layer-index";

Glib::RefPtr<Gdk::Pixbuf> thumb_pixbuf(const Layer& layer) {
  const int w = layer.thumbnail_width();
  const int h = layer.thumbnail_height();
  auto pixbuf = Gdk::Pixbuf::create(Gdk::COLORSPACE_RGB, true, 8, w, h);
  const int dst_stride = pixbuf->get_rowstride();
  std::uint8_t* dst = pixbuf->get_pixels();
  const std::uint8_t* src = layer.thumbnail();
  for (int y = 0; y < h; ++y) {
    std::memcpy(dst + static_cast<std::size_t>(y) * dst_stride,
                src + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 4,
                static_cast<std::size_t>(w) * 4);
  }
  return pixbuf;
}

Gtk::Window* toplevel_window(Gtk::Widget& widget) {
  auto* top = widget.get_toplevel();
  if (top != nullptr && top->get_is_toplevel()) {
    return dynamic_cast<Gtk::Window*>(top);
  }
  return nullptr;
}

class LayerRow : public Gtk::ListBoxRow {
public:
  explicit LayerRow(int stack_index) : stack_index(stack_index) {}
  int stack_index = 0;
};

}  // namespace

void LayersPanel::style_icon_button(Gtk::Button& button, const std::string& icon_name,
                                    const char* tip) {
  const std::string resource =
      "/org/lunduke/LundukePaint/icons/scalable/actions/" + icon_name + ".svg";
  auto* image = Gtk::manage(new Gtk::Image());
  try {
    auto pixbuf = Gdk::Pixbuf::create_from_resource(resource, 18, 18, true);
    image->set(pixbuf);
  } catch (const Glib::Error&) {
    image->set_from_resource(resource);
    image->set_pixel_size(18);
  }
  button.set_image(*image);
  button.set_tooltip_text(tip);
  button.set_can_focus(false);
  button.set_relief(Gtk::RELIEF_NONE);
  button.set_size_request(28, 28);
}

LayersPanel::LayersPanel() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2) {
  set_border_width(3);
  set_vexpand(true);
  list_.set_selection_mode(Gtk::SELECTION_SINGLE);
  list_.set_activate_on_single_click(true);
  list_.signal_row_selected().connect(sigc::mem_fun(*this, &LayersPanel::on_row_selected));
  scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll_.set_min_content_height(160);
  scroll_.set_vexpand(true);
  scroll_.add(list_);

  std::vector<Gtk::TargetEntry> targets;
  targets.emplace_back(kLayerDnD, Gtk::TARGET_SAME_APP, 0);
  list_.drag_dest_set(targets, Gtk::DEST_DEFAULT_MOTION | Gtk::DEST_DEFAULT_HIGHLIGHT,
                      Gdk::ACTION_MOVE);
  list_.signal_drag_motion().connect(sigc::mem_fun(*this, &LayersPanel::on_list_drag_motion));
  list_.signal_drag_leave().connect(sigc::mem_fun(*this, &LayersPanel::on_list_drag_leave));
  list_.signal_drag_drop().connect(sigc::mem_fun(*this, &LayersPanel::on_list_drag_drop));
  list_.signal_drag_data_received().connect(
      sigc::mem_fun(*this, &LayersPanel::on_list_drag_data_received));

  style_icon_button(del_, "layer-delete-symbolic", "Delete layer");
  style_icon_button(rename_, "layer-rename-symbolic", "Rename layer");
  style_icon_button(add_, "layer-add-symbolic", "Add layer");

  // Right-justified: Delete, Rename, Add (left → right).
  toolbar_.set_halign(Gtk::ALIGN_END);
  toolbar_.set_hexpand(true);
  toolbar_.pack_start(del_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(rename_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(add_, Gtk::PACK_SHRINK);

  add_.signal_clicked().connect(sigc::mem_fun(*this, &LayersPanel::add_layer_clicked));
  del_.signal_clicked().connect(sigc::mem_fun(*this, &LayersPanel::delete_layer_clicked));
  rename_.signal_clicked().connect(sigc::mem_fun(*this, &LayersPanel::rename_clicked));

  pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);
  pack_start(toolbar_, Gtk::PACK_SHRINK);
}

void LayersPanel::add_layer_clicked() {
  if (document_ == nullptr) {
    return;
  }
  if (document_->layers().count() >= kSoftMaxLayers) {
    if (auto* win = toplevel_window(*this)) {
      Gtk::MessageDialog warn(*win,
                              "This document has 64 or more layers and may use a lot of memory.",
                              false, Gtk::MESSAGE_WARNING, Gtk::BUTTONS_OK_CANCEL, true);
      if (warn.run() != Gtk::RESPONSE_OK) {
        return;
      }
    }
  }
  document_->add_layer();
}

void LayersPanel::delete_layer_clicked() {
  if (document_ != nullptr) {
    document_->delete_layer();
  }
}

void LayersPanel::rename_clicked() {
  if (document_ == nullptr) {
    return;
  }
  rename_layer(document_->layers().active_index());
}

void LayersPanel::set_document(Document* document) {
  document_ = document;
  refresh();
}

int LayersPanel::selected_stack_index() const {
  auto* row = list_.get_selected_row();
  if (row == nullptr) {
    return document_ != nullptr ? document_->layers().active_index() : 0;
  }
  if (auto* layer_row = dynamic_cast<const LayerRow*>(row)) {
    return layer_row->stack_index;
  }
  return 0;
}

void LayersPanel::on_row_selected(Gtk::ListBoxRow* row) {
  if (refreshing_ || document_ == nullptr || row == nullptr) {
    return;
  }
  if (auto* layer_row = dynamic_cast<const LayerRow*>(row)) {
    document_->layers().set_active_index(layer_row->stack_index);
    document_->notify_changed();
  }
}

void LayersPanel::on_row_drag_begin(const Glib::RefPtr<Gdk::DragContext>& /*context*/,
                                    Gtk::ListBoxRow* row) {
  drag_stack_from_ = -1;
  if (auto* layer_row = dynamic_cast<LayerRow*>(row)) {
    drag_stack_from_ = layer_row->stack_index;
  }
}

void LayersPanel::on_row_drag_data_get(const Glib::RefPtr<Gdk::DragContext>& /*context*/,
                                       Gtk::SelectionData& selection_data, guint /*info*/,
                                       guint /*time*/, int stack_index) {
  const int value = stack_index;
  selection_data.set(kLayerDnD, 8, reinterpret_cast<const guchar*>(&value), sizeof(value));
}

bool LayersPanel::on_list_drag_motion(const Glib::RefPtr<Gdk::DragContext>& context, int /*x*/, int y,
                                      guint time) {
  auto* row = list_.get_row_at_y(y);
  if (row != nullptr) {
    gtk_list_box_drag_highlight_row(list_.gobj(), row->gobj());
  } else {
    gtk_list_box_drag_unhighlight_row(list_.gobj());
  }
  context->drag_status(Gdk::ACTION_MOVE, time);
  return true;
}

void LayersPanel::on_list_drag_leave(const Glib::RefPtr<Gdk::DragContext>& /*context*/,
                                     guint /*time*/) {
  gtk_list_box_drag_unhighlight_row(list_.gobj());
}

bool LayersPanel::on_list_drag_drop(const Glib::RefPtr<Gdk::DragContext>& context, int /*x*/,
                                    int /*y*/, guint time) {
  if (context->list_targets().empty()) {
    return false;
  }
  // Request the payload; drop completes in drag_data_received.
  for (const auto& target : context->list_targets()) {
    if (target == kLayerDnD) {
      list_.drag_get_data(context, target, time);
      return true;
    }
  }
  return false;
}

void LayersPanel::on_list_drag_data_received(const Glib::RefPtr<Gdk::DragContext>& context, int x,
                                             int y, const Gtk::SelectionData& data, guint /*info*/,
                                             guint time) {
  gtk_list_box_drag_unhighlight_row(list_.gobj());
  bool success = false;
  if (document_ != nullptr && data.get_length() >= static_cast<int>(sizeof(int)) &&
      data.get_data_type() == kLayerDnD) {
    int from = -1;
    std::memcpy(&from, data.get_data(), sizeof(from));
    auto* dest_row = list_.get_row_at_y(y);
    int to = from;
    if (dest_row != nullptr) {
      if (auto* layer_row = dynamic_cast<LayerRow*>(dest_row)) {
        to = layer_row->stack_index;
      }
    } else if (document_->layers().count() > 0) {
      // Drop below last UI row → bottom of stack (index 0).
      to = 0;
    }
    if (from >= 0 && to >= 0 && from != to) {
      success = document_->move_layer(from, to);
    }
  }
  (void)x;
  context->drag_finish(success, false, time);
  drag_stack_from_ = -1;
}

void LayersPanel::add_row(int stack_index) {
  if (document_ == nullptr) {
    return;
  }
  Layer& layer = document_->layers().at(stack_index);
  auto* row = Gtk::manage(new LayerRow(stack_index));
  auto* box = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_VERTICAL, 1));
  box->set_border_width(1);

  auto* image = Gtk::manage(new Gtk::Image());
  auto full = thumb_pixbuf(layer);
  image->set(full->scale_simple(24, 18, Gdk::INTERP_NEAREST));
  image->set_halign(Gtk::ALIGN_START);
  image->set_valign(Gtk::ALIGN_CENTER);

  auto* name = Gtk::manage(new Gtk::Label(layer.name()));
  name->set_xalign(0.0f);
  name->set_ellipsize(Pango::ELLIPSIZE_END);
  name->set_max_width_chars(18);

  auto* eye = Gtk::manage(new Gtk::ToggleButton());
  eye->set_image_from_icon_name("view-reveal-symbolic", Gtk::ICON_SIZE_MENU);
  eye->set_tooltip_text("Visible");
  eye->set_active(layer.visible());
  eye->set_can_focus(false);
  eye->signal_toggled().connect([this, stack_index, eye]() {
    if (refreshing_ || document_ == nullptr) {
      return;
    }
    document_->set_layer_visible(stack_index, eye->get_active());
  });

  auto* lock = Gtk::manage(new Gtk::ToggleButton());
  lock->set_image_from_icon_name("changes-prevent-symbolic", Gtk::ICON_SIZE_MENU);
  lock->set_tooltip_text("Locked");
  lock->set_active(layer.locked());
  lock->set_can_focus(false);
  lock->signal_toggled().connect([this, stack_index, lock]() {
    if (refreshing_ || document_ == nullptr) {
      return;
    }
    document_->set_layer_locked(stack_index, lock->get_active());
  });

  // Thumbnail sits on the same row as visibility + lock (not above the name).
  auto* icons = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 4));
  icons->set_halign(Gtk::ALIGN_START);
  icons->pack_start(*image, Gtk::PACK_SHRINK);
  icons->pack_start(*eye, Gtk::PACK_SHRINK);
  icons->pack_start(*lock, Gtk::PACK_SHRINK);

  box->pack_start(*name, Gtk::PACK_SHRINK);
  box->pack_start(*icons, Gtk::PACK_SHRINK);
  row->add(*box);
  row->add_events(Gdk::BUTTON_PRESS_MASK);
  row->signal_button_press_event().connect(
      [this, stack_index](GdkEventButton* event) {
        if (event != nullptr && event->button == 3) {
          popup_row_menu(stack_index, event);
          return true;
        }
        return false;
      },
      false);

  std::vector<Gtk::TargetEntry> targets;
  targets.emplace_back(kLayerDnD, Gtk::TARGET_SAME_APP, 0);
  row->drag_source_set(targets, Gdk::BUTTON1_MASK, Gdk::ACTION_MOVE);
  row->signal_drag_begin().connect(
      [this, row](const Glib::RefPtr<Gdk::DragContext>& ctx) { on_row_drag_begin(ctx, row); });
  row->signal_drag_data_get().connect(
      [this, stack_index](const Glib::RefPtr<Gdk::DragContext>& ctx, Gtk::SelectionData& sel,
                          guint info, guint time) {
        on_row_drag_data_get(ctx, sel, info, time, stack_index);
      });

  row->show_all();
  list_.append(*row);
}

void LayersPanel::popup_row_menu(int stack_index, GdkEventButton* event) {
  if (document_ == nullptr) {
    return;
  }
  document_->layers().set_active_index(stack_index);
  document_->notify_changed();

  auto menu = std::make_shared<Gtk::Menu>();
  auto add_item = [&](const char* label, std::function<void()> fn) {
    auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
    item->signal_activate().connect(std::move(fn));
    menu->append(*item);
  };
  add_item("_Add layer", [this]() { add_layer_clicked(); });
  add_item("_Duplicate", [this]() { document_->duplicate_layer(); });
  add_item("De_lete", [this]() { document_->delete_layer(); });
  add_item("_Raise", [this]() { document_->raise_layer(); });
  add_item("_Lower", [this]() { document_->lower_layer(); });
  add_item("_Merge down", [this]() { document_->merge_down(); });
  add_item("_Flatten", [this]() { document_->flatten(); });
  menu->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item("Re_name…", [this, stack_index]() { rename_layer(stack_index); });
  add_item("_Properties…", [this]() { show_properties(); });
  menu->show_all();
  menu->attach_to_widget(*this);
  if (event != nullptr) {
    menu->popup(event->button, event->time);
  } else {
    menu->popup(0, gtk_get_current_event_time());
  }
  menu->signal_hide().connect([menu]() mutable { menu.reset(); });
}

void LayersPanel::rename_layer(int stack_index) {
  if (document_ == nullptr) {
    return;
  }
  auto* win = toplevel_window(*this);
  Gtk::Dialog dialog("Rename layer", false);
  if (win != nullptr) {
    dialog.set_transient_for(*win);
  }
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_Rename", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);
  Gtk::Entry entry;
  entry.set_text(document_->layers().at(stack_index).name());
  entry.set_activates_default(true);
  dialog.get_content_area()->set_border_width(8);
  dialog.get_content_area()->pack_start(entry, Gtk::PACK_SHRINK);
  dialog.show_all();
  if (dialog.run() == Gtk::RESPONSE_OK) {
    document_->rename_layer(stack_index, entry.get_text());
  }
}

void LayersPanel::show_properties() {
  if (document_ == nullptr) {
    return;
  }
  const int index = document_->layers().active_index();
  Layer& layer = document_->layers().at(index);
  auto* win = toplevel_window(*this);
  Gtk::Dialog dialog("Layer properties", false);
  if (win != nullptr) {
    dialog.set_transient_for(*win);
  }
  dialog.add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  dialog.add_button("_OK", Gtk::RESPONSE_OK);
  dialog.set_default_response(Gtk::RESPONSE_OK);

  Gtk::Box box(Gtk::ORIENTATION_VERTICAL, 6);
  box.set_border_width(8);
  Gtk::Entry name;
  name.set_text(layer.name());
  Gtk::SpinButton opacity;
  opacity.set_range(0, 100);
  opacity.set_increments(1, 10);
  opacity.set_digits(0);
  opacity.set_value(static_cast<int>(layer.opacity() * 100.0f + 0.5f));
  Gtk::ComboBoxText blend;
  for (int i = 0; i < kBlendModeCount; ++i) {
    blend.append(blend_mode_label(blend_mode_from_index(i)));
  }
  blend.set_active(static_cast<int>(layer.blend()));
  box.pack_start(*Gtk::manage(new Gtk::Label("Name", 0.0, 0.5)), Gtk::PACK_SHRINK);
  box.pack_start(name, Gtk::PACK_SHRINK);
  box.pack_start(*Gtk::manage(new Gtk::Label("Opacity", 0.0, 0.5)), Gtk::PACK_SHRINK);
  box.pack_start(opacity, Gtk::PACK_SHRINK);
  Gtk::SpinButton offx;
  offx.set_range(-16384, 16384);
  offx.set_increments(1, 16);
  offx.set_digits(0);
  offx.set_value(layer.offset_x());
  Gtk::SpinButton offy;
  offy.set_range(-16384, 16384);
  offy.set_increments(1, 16);
  offy.set_digits(0);
  offy.set_value(layer.offset_y());
  box.pack_start(*Gtk::manage(new Gtk::Label("Blend", 0.0, 0.5)), Gtk::PACK_SHRINK);
  box.pack_start(blend, Gtk::PACK_SHRINK);
  box.pack_start(*Gtk::manage(new Gtk::Label("Offset X", 0.0, 0.5)), Gtk::PACK_SHRINK);
  box.pack_start(offx, Gtk::PACK_SHRINK);
  box.pack_start(*Gtk::manage(new Gtk::Label("Offset Y", 0.0, 0.5)), Gtk::PACK_SHRINK);
  box.pack_start(offy, Gtk::PACK_SHRINK);
  dialog.get_content_area()->pack_start(box, Gtk::PACK_SHRINK);
  dialog.show_all();
  if (dialog.run() != Gtk::RESPONSE_OK) {
    return;
  }
  document_->rename_layer(index, name.get_text());
  document_->set_layer_opacity(index, static_cast<float>(opacity.get_value_as_int()) / 100.0f);
  document_->set_layer_blend(index, blend_mode_from_index(blend.get_active_row_number()));
  document_->set_layer_offset(index, offx.get_value_as_int(), offy.get_value_as_int());
}

void LayersPanel::update_buttons() {
  const bool have = document_ != nullptr && document_->layers().count() > 0;
  const int count = have ? document_->layers().count() : 0;
  del_.set_sensitive(have && count > 1);
  rename_.set_sensitive(have);
  add_.set_sensitive(document_ != nullptr);
}

void LayersPanel::refresh() {
  if (refreshing_) {
    return;
  }
  refreshing_ = true;
  std::vector<Gtk::Widget*> old;
  for (auto* child : list_.get_children()) {
    old.push_back(child);
  }
  for (auto* child : old) {
    list_.remove(*child);
  }
  if (document_ != nullptr) {
    for (int ui = 0; ui < document_->layers().count(); ++ui) {
      const int stack = document_->layers().count() - 1 - ui;
      add_row(stack);
    }
    const int active = document_->layers().active_index();
    const int ui = document_->layers().count() - 1 - active;
    if (auto* row = list_.get_row_at_index(ui)) {
      list_.select_row(*row);
    }
  }
  list_.show_all();
  refreshing_ = false;
  update_buttons();
}

}  // namespace lundukepaint
