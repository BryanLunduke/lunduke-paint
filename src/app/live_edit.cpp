// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/live_edit.hpp"

#include "doc/document.hpp"
#include "tools/tool.hpp"

namespace lundukepaint {

LiveKind classify_live(const Tool* tool, const Document* document) {
  const bool text = tool != nullptr && tool->captures_keys();
  const bool text_nonempty =
      text && ((document != nullptr && document->unsaved_overlay()) ||
               (tool != nullptr && tool->has_uncommitted_preview()));
  const bool persistent = tool != nullptr && tool->has_uncommitted_preview();
  const bool drag = tool != nullptr && tool->is_stroking() && tool->uses_tool_layer();
  const bool floating = document != nullptr && document->selection().floating();
  return classify_live_flags(text, text_nonempty, persistent, drag, floating);
}

SettleResult settle_tool_live(Tool* tool, Document* document, LivePath path) {
  const LiveKind kind = classify_live(tool, document);
  switch (live_edit_disposition(kind, path)) {
    case LiveDisposition::None:
    case LiveDisposition::Leave:
    case LiveDisposition::Composite:
      return SettleResult::Proceed;
    case LiveDisposition::Prompt:
      return SettleResult::Prompt;
    case LiveDisposition::Keep:
      return SettleResult::Blocked;
    case LiveDisposition::CancelEmpty:
      if (tool != nullptr) {
        tool->on_cancel();
      }
      return SettleResult::Proceed;
    case LiveDisposition::Stamp:
      break;
  }
  if (tool != nullptr &&
      (tool->captures_keys() || tool->has_uncommitted_preview() ||
       (tool->is_stroking() && tool->uses_tool_layer()))) {
    if (!tool->on_commit()) {
      return SettleResult::Blocked;
    }
  }
  if (document != nullptr && document->selection().floating() &&
      !document->try_commit_floating()) {
    document->notify_blocked("Unlock the layer to place the selection");
    return SettleResult::Blocked;
  }
  return SettleResult::Proceed;
}

}  // namespace lundukepaint
