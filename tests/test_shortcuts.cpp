// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/shortcut_dispatch.hpp"

#include <cstdio>
#include <cstring>

namespace {

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_shortcuts: %s\n", msg);
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  using lundukepaint::CanvasKeyAction;
  using lundukepaint::ToolKey;
  using lundukepaint::resolve_canvas_key;

  // 'D' and 'X' stay color commands even when a tool lists them.
  const ToolKey tools[] = {
      {"freeform", 'D'}, {"rect", 'R'}, {"rect-fill", 'R'},
      {"rounded", 'U'}, {"rounded-fill", 'U'},
  };
  const int count = static_cast<int>(sizeof(tools) / sizeof(tools[0]));

  int errors = 0;
  const auto reset = resolve_canvas_key('D', tools, count, "freeform");
  errors += expect(reset.action == CanvasKeyAction::ResetColors, "D resets colors");
  errors += expect(reset.tool_id == nullptr, "D does not select the tool that lists it");

  const auto swap = resolve_canvas_key('X', tools, count, "rect");
  errors += expect(swap.action == CanvasKeyAction::SwapColors, "X swaps colors");
  errors += expect(swap.tool_id == nullptr, "X does not select a tool");

  const auto first_u = resolve_canvas_key('U', tools, count, nullptr);
  errors += expect(first_u.action == CanvasKeyAction::SelectTool, "U selects a tool");
  errors += expect(first_u.tool_id != nullptr && std::strcmp(first_u.tool_id, "rounded") == 0,
                   "U starts at the first shared tool");

  const auto next_u = resolve_canvas_key('U', tools, count, "rounded");
  errors += expect(next_u.tool_id != nullptr && std::strcmp(next_u.tool_id, "rounded-fill") == 0,
                   "U again reaches the other tool that shares the key");

  const auto wrap_u = resolve_canvas_key('U', tools, count, "rounded-fill");
  errors += expect(wrap_u.tool_id != nullptr && std::strcmp(wrap_u.tool_id, "rounded") == 0,
                   "U wraps back to the first shared tool");

  const auto next_r = resolve_canvas_key('R', tools, count, "rect");
  errors += expect(next_r.tool_id != nullptr && std::strcmp(next_r.tool_id, "rect-fill") == 0,
                   "a shared letter reaches the second tool");

  if (errors != 0) {
    std::fprintf(stderr, "test_shortcuts: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_shortcuts: ok\n");
  return 0;
}
