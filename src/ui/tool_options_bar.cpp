// SPDX-License-Identifier: GPL-3.0-or-later

#include "ui/tool_options_bar.hpp"

#include "tools/tool.hpp"

namespace lundukepaint {

ToolOptionsBar::ToolOptionsBar() : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 6) {
  set_border_width(4);
  set_size_request(-1, 28);
  get_style_context()->add_class("toolbar");
  placeholder_.set_text("");
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
    pack_start(*options, Gtk::PACK_SHRINK);
    options->show_all();
    current_ = options;
    placeholder_.hide();
  } else {
    placeholder_.show();
  }
}

}  // namespace lundukepaint
