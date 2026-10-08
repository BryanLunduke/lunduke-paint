// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_UI_PALETTE_CLICK_HPP
#define LUNDUKEPAINT_UI_PALETTE_CLICK_HPP

namespace lundukepaint {

// What a palette swatch click does. Right-click sets the background and
// never stores into the cell. Editing a cell is an explicit double-click.
enum class PaletteClick { Ignore, Foreground, Background, Edit };

inline PaletteClick palette_click(unsigned button, bool shift, bool double_click) {
  if (double_click && button == 1) {
    return PaletteClick::Edit;
  }
  if (button == 3 || button == 2 || (button == 1 && shift)) {
    return PaletteClick::Background;
  }
  if (button == 1) {
    return PaletteClick::Foreground;
  }
  return PaletteClick::Ignore;
}

}  // namespace lundukepaint

#endif
