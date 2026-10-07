// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_SHORTCUT_DISPATCH_HPP
#define LUNDUKEPAINT_APP_SHORTCUT_DISPATCH_HPP

#include <cstring>

namespace lundukepaint {

enum class CanvasKeyAction { None, SwapColors, ResetColors, SelectTool };

struct ToolKey {
  const char* id = nullptr;
  char key = 0;
};

struct CanvasKeyResult {
  CanvasKeyAction action = CanvasKeyAction::None;
  const char* tool_id = nullptr;
};

// 'X' swaps colors and 'D' resets them. Those letters never select a tool,
// even if a tool lists them. When several tools share another letter, the
// result is the next match after `active_id` (wrapping), so each one is
// reachable.
inline CanvasKeyResult resolve_canvas_key(char ch, const ToolKey* tools, int count,
                                          const char* active_id) {
  CanvasKeyResult result;
  if (ch == 'X') {
    result.action = CanvasKeyAction::SwapColors;
    return result;
  }
  if (ch == 'D') {
    result.action = CanvasKeyAction::ResetColors;
    return result;
  }
  if (tools == nullptr || count < 1 || ch == 0) {
    return result;
  }
  int first = -1;
  int active_match = -1;
  int matches = 0;
  for (int i = 0; i < count; ++i) {
    if (tools[i].key != ch) {
      continue;
    }
    if (first < 0) {
      first = i;
    }
    ++matches;
    if (active_id != nullptr && tools[i].id != nullptr && std::strcmp(tools[i].id, active_id) == 0) {
      active_match = i;
    }
  }
  if (first < 0) {
    return result;
  }
  int chosen = first;
  if (active_match >= 0 && matches > 1) {
    chosen = -1;
    for (int i = active_match + 1; i < count; ++i) {
      if (tools[i].key == ch) {
        chosen = i;
        break;
      }
    }
    if (chosen < 0) {
      chosen = first;
    }
  }
  result.action = CanvasKeyAction::SelectTool;
  result.tool_id = tools[chosen].id;
  return result;
}

}  // namespace lundukepaint

#endif
