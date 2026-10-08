// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_SHORTCUT_HELP_HPP
#define LUNDUKEPAINT_APP_SHORTCUT_HELP_HPP

namespace lundukepaint {

struct ShortcutHelpRow {
  const char* action;
  const char* keys;
};

// Help → Keyboard Shortcuts. U is only the rounded-rectangle cycle.
// Esc cancels an in-progress edit; it deselects only when nothing is open.
inline const ShortcutHelpRow* shortcut_help_rows(int& count) {
  static const ShortcutHelpRow kRows[] = {
      {"New / Open / Save / Save As", "Ctrl+N / O / S / Shift+S"},
      {"Close / Quit", "Ctrl+W / Q"},
      {"Print", "Ctrl+P"},
      {"Undo", "Ctrl+Z"},
      {"Redo", "Ctrl+Y or Ctrl+Shift+Z"},
      {"Cut / Copy / Paste / Select all", "Ctrl+X / C / V / A"},
      {"Deselect", "Ctrl+D"},
      {"Escape",
       "Cancels the text box, the in-progress shape, or a resting float; deselects only when nothing is in progress"},
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
      {"Line / Rectangle outline / filled", "L / R / J"},
      {"Ellipse outline / filled", "E / Z"},
      {"Freeform outline / filled", "K / O"},
      {"Polygon outline / filled", "G / Q"},
      {"Rounded rectangle outline, press U again for filled", "U"},
      {"Text / Curve", "T / V"},
      {"Spray / Polyline", "Y / N"},
  };
  count = static_cast<int>(sizeof(kRows) / sizeof(kRows[0]));
  return kRows;
}

}  // namespace lundukepaint

#endif
