// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_TOOL_SELECTION_HPP
#define LUNDUKEPAINT_UI_TOOL_SELECTION_HPP

#include <cstddef>
#include <string>
#include <vector>

// Widget-free bookkeeping for the toolbox' selected-tool highlight, so the
// "which button is lit" logic can be tested without a display.
namespace lundukepaint {
namespace toolbox_style {

// CSS class carried by every tool button (reserves the selected border so the
// grid does not jump when the highlight moves).
inline const char* button_class() {
  return "tool-button";
}

// CSS class carried by the one selected tool button.
inline const char* selected_class() {
  return "tool-selected";
}

// Small caption class used by the short FG / BG labels beside the color wells.
inline const char* caption_class() {
  return "toolbox-caption";
}

// Explicit, theme-independent look for the selected tool: a light blue fill, a
// dark blue border and an inset shadow, so the pressed tool reads as pressed
// under Adwaita, Greybird, Raleigh or any other GTK3 theme.
inline const char* css() {
  return
      ".tool-button {"
      "  border: 2px solid transparent;"
      "  border-radius: 3px;"
      "  padding: 0px;"
      "  min-width: 24px;"
      "  min-height: 24px;"
      "  background-image: none;"
      "}"
      ".tool-button.tool-selected {"
      "  background-image: linear-gradient(to bottom, #d3e4fb, #a9c7ee);"
      "  background-color: #bcd6f5;"
      "  border: 2px solid #2a5d9f;"
      "  box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.35);"
      "}"
      ".tool-button.tool-selected:hover {"
      "  background-image: linear-gradient(to bottom, #dcebff, #b6d1f4);"
      "  border: 2px solid #2a5d9f;"
      "}"
      ".toolbox-caption {"
      "  font-size: 7.5pt; padding: 0;"
      "}"
      ".toolbox-options, .toolbox-options * {"
      "  min-width: 0;"
      "}"
      ".toolbox-options spinbutton,"
      ".toolbox-options entry,"
      ".toolbox-options combobox,"
      ".toolbox-options button {"
      "  min-width: 0;"
      "  padding-left: 1px;"
      "  padding-right: 1px;"
      "}";
}

}  // namespace toolbox_style

// Index of id in ids, or -1. Used to decide which button gets lit.
inline int tool_index(const std::vector<std::string>& ids, const std::string& id) {
  for (std::size_t i = 0; i < ids.size(); ++i) {
    if (ids[i] == id) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Bookkeeping half of Toolbox::set_active_tool: remembers the selected id and
// reports the button that should be lit. An unknown id leaves the previous
// selection alone, so a stray id can never blank the toolbox out.
class ToolSelection {
public:
  void add(const std::string& id) { ids_.push_back(id); }

  const std::vector<std::string>& ids() const { return ids_; }
  const std::string& active_id() const { return active_id_; }
  int active_index() const { return tool_index(ids_, active_id_); }
  bool is_selected(const std::string& id) const { return !active_id_.empty() && id == active_id_; }

  // Returns true when the selection actually moved.
  bool select(const std::string& id) {
    if (tool_index(ids_, id) < 0) {
      return false;
    }
    if (id == active_id_) {
      return false;
    }
    active_id_ = id;
    return true;
  }

private:
  std::vector<std::string> ids_;
  std::string active_id_;
};

}  // namespace lundukepaint

#endif
