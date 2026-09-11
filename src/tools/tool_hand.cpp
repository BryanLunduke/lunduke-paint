// SPDX-License-Identifier: GPL-3.0-or-later

#include "tools/tool.hpp"

namespace lundukepaint {

// Hand / pan tool: CanvasView treats this id like Space+drag and pans the view.
class HandTool : public Tool {
public:
  const char* id() const override { return "hand"; }
  const char* name() const override { return "Hand"; }
  char shortcut() const override { return 'H'; }
  const char* hint() const override { return "Hand: drag to pan the canvas (also Space+drag / middle mouse)"; }

  void on_press(CanvasEvent /*event*/) override {}
  void on_motion(CanvasEvent /*event*/) override {}
  void on_release(CanvasEvent /*event*/) override {}
  void on_cancel() override {}
};

Tool* create_hand_tool() { return new HandTool(); }

}  // namespace lundukepaint
