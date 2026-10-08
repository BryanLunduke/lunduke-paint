// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/history_panel.hpp"

#include "doc/document.hpp"
#include "doc/history.hpp"

#include <gtkmm/label.h>
#include <gtkmm/listboxrow.h>

namespace lundukepaint {
namespace {

class HistoryRow : public Gtk::ListBoxRow {
public:
  HistoryRow(int history_index, const Glib::ustring& name, bool current, bool future)
      : history_index(history_index) {
    auto* label = Gtk::manage(new Gtk::Label(name));
    label->set_xalign(0.0f);
    label->set_ellipsize(Pango::ELLIPSIZE_END);
    if (future) {
      label->set_opacity(0.45);
    }
    add(*label);
    if (current) {
      get_style_context()->add_class("suggested-action");
    }
    show_all();
  }

  int history_index = -1;
};

}  // namespace

HistoryPanel::HistoryPanel() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 4) {
  set_border_width(3);
  list_.set_selection_mode(Gtk::SELECTION_SINGLE);
  list_.set_activate_on_single_click(true);
  list_.signal_row_activated().connect(sigc::mem_fun(*this, &HistoryPanel::on_row_activated));
  scroll_.set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
  scroll_.set_min_content_height(80);
  scroll_.set_propagate_natural_height(false);
  scroll_.add(list_);
  notice_.set_text(kUndoDroppedNotice);
  notice_.set_xalign(0.0f);
  notice_.set_line_wrap(true);
  notice_.set_max_width_chars(28);
  notice_.get_style_context()->add_class("dim-label");
  notice_.set_no_show_all(true);
  notice_.hide();
  pack_start(notice_, Gtk::PACK_SHRINK);
  pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);
}

void HistoryPanel::set_document(Document* document) {
  document_ = document;
  refresh();
}

void HistoryPanel::refresh() {
  refreshing_ = true;
  const std::vector<Gtk::Widget*> children = list_.get_children();
  for (Gtk::Widget* child : children) {
    list_.remove(*child);
  }

  const int current = document_ != nullptr ? document_->history().index() : -1;
  const int count = document_ != nullptr ? document_->history().count() : 0;
  const bool dropped = document_ != nullptr && document_->history().base_dropped();
  if (dropped) {
    notice_.show();
  } else {
    notice_.hide();
  }
  const char* base = document_ != nullptr ? document_->history().base_label() : "New document";

  auto* initial = Gtk::manage(new HistoryRow(-1, base, current == -1, false));
  list_.append(*initial);

  for (int i = 0; i < count; ++i) {
    const bool is_current = (i == current);
    const bool future = (i > current);
    auto* row = Gtk::manage(new HistoryRow(i, document_->history().name_at(i), is_current, future));
    list_.append(*row);
  }

  list_.show_all();
  const int select = current + 1;  // row 0 is the oldest restorable state
  if (Gtk::ListBoxRow* row = list_.get_row_at_index(select)) {
    list_.select_row(*row);
  }
  refreshing_ = false;
}

void HistoryPanel::on_row_activated(Gtk::ListBoxRow* row) {
  if (refreshing_ || row == nullptr || !on_jump) {
    return;
  }
  auto* hrow = dynamic_cast<HistoryRow*>(row);
  if (hrow == nullptr) {
    return;
  }
  on_jump(hrow->history_index);
}

}  // namespace lundukepaint
