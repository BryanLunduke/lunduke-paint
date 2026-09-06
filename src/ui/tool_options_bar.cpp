// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/tool_options_bar.hpp"

#include "tools/tool.hpp"

namespace lundukepaint {

ToolOptionsBar::ToolOptionsBar() : Gtk::Box(Gtk::ORIENTATION_VERTICAL, 2) {
  set_border_width(2);
  set_hexpand(false);
  set_halign(Gtk::ALIGN_FILL);
  set_valign(Gtk::ALIGN_START);
  get_style_context()->add_class("tool-options");
  placeholder_.set_text("");
  placeholder_.set_halign(Gtk::ALIGN_START);
  pack_start(placeholder_, Gtk::PACK_SHRINK);
}

void ToolOptionsBar::show_tool(Tool* tool) {
  if (current_ != nullptr) {
    remove(*current_);
    current_ = nullptr;
  }
  Gtk::Widget* options = tool != nullptr ? tool->options_widget() : nullptr;
  if (options != nullptr) {
    if (options->get_parent() != nullptr && options->get_parent() != this) {
      options->get_parent()->remove(*options);
    }
    options->set_hexpand(false);
    options->set_halign(Gtk::ALIGN_FILL);
    pack_start(*options, Gtk::PACK_SHRINK);
    options->show_all();
    current_ = options;
    placeholder_.hide();
  } else {
    placeholder_.show();
  }
}

}  // namespace lundukepaint
