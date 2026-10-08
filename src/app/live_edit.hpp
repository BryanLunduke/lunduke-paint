// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_LIVE_EDIT_HPP
#define LUNDUKEPAINT_APP_LIVE_EDIT_HPP

namespace lundukepaint {

class Document;
class Tool;

// What to do with an open text box or an in-progress polygon / polyline / curve.
enum class LiveEditAction { Leave, Stamp, Cancel };

enum class CloseAnswer { Cancel, Discard, Save };

// Quit and Close ask first. Cancel must not stamp. Save stamps, then writes.
// Discard drops the overlay with the document.
inline LiveEditAction live_edit_for_close(CloseAnswer answer) {
  switch (answer) {
    case CloseAnswer::Cancel:
      return LiveEditAction::Leave;
    case CloseAnswer::Save:
      return LiveEditAction::Stamp;
    case CloseAnswer::Discard:
      return LiveEditAction::Cancel;
  }
  return LiveEditAction::Leave;
}

// Switching tabs leaves the edit on the document it was typed into.
// An empty text box is closed. A word, or a polygon that is already on
// screen, is stamped as one undo step.
inline LiveEditAction live_edit_for_tab_switch(bool text_editing, bool text_nonempty,
                                              bool persistent_preview) {
  if (text_editing) {
    return text_nonempty ? LiveEditAction::Stamp : LiveEditAction::Cancel;
  }
  if (persistent_preview) {
    return LiveEditAction::Stamp;
  }
  return LiveEditAction::Leave;
}

// Kinds of work that are on screen but not yet a history entry on a layer.
enum class LiveKind {
  None,
  DragShape,
  PersistentShape,
  TextEmpty,
  TextContent,
  Floating,
};

// Paths that can drop, move, or hide that work.
enum class LivePath {
  ToolChange,
  Undo,
  Redo,
  HistoryJump,
  Open,
  NewDocument,
  CloseTab,
  TabSwitch,
  Quit,
  LayerChange,
  LayerAdd,
  LayerDelete,
  LayerMove,
  LayerLock,
  Save,
  Recovery,
  Paste,
  Revert,
  Transform,
};

// Stamp: commit onto the layer that owns the edit. If that fails, abort.
// Keep: leave the edit and do not perform the path (undo must not also step).
// Leave: leave the edit and let the path proceed (locking the layer).
// Prompt: ask. Cancel leaves the edit, Save stamps, Discard drops it.
// Composite: bake into a snapshot without committing.
// CancelEmpty: an empty text box is not content; close it and proceed.
enum class LiveDisposition {
  None,
  Stamp,
  Keep,
  Leave,
  Prompt,
  Composite,
  CancelEmpty,
};

enum class SettleResult { Proceed, Blocked, Prompt };

inline const char* live_kind_name(LiveKind kind) {
  switch (kind) {
    case LiveKind::None:
      return "none";
    case LiveKind::DragShape:
      return "drag";
    case LiveKind::PersistentShape:
      return "shape";
    case LiveKind::TextEmpty:
      return "text-empty";
    case LiveKind::TextContent:
      return "text";
    case LiveKind::Floating:
      return "float";
  }
  return "none";
}

inline const char* live_path_name(LivePath path) {
  switch (path) {
    case LivePath::ToolChange:
      return "tool-change";
    case LivePath::Undo:
      return "undo";
    case LivePath::Redo:
      return "redo";
    case LivePath::HistoryJump:
      return "history-jump";
    case LivePath::Open:
      return "open";
    case LivePath::NewDocument:
      return "new";
    case LivePath::CloseTab:
      return "close-tab";
    case LivePath::TabSwitch:
      return "tab-switch";
    case LivePath::Quit:
      return "quit";
    case LivePath::LayerChange:
      return "layer-change";
    case LivePath::LayerAdd:
      return "layer-add";
    case LivePath::LayerDelete:
      return "layer-delete";
    case LivePath::LayerMove:
      return "layer-move";
    case LivePath::LayerLock:
      return "layer-lock";
    case LivePath::Save:
      return "save";
    case LivePath::Recovery:
      return "recovery";
    case LivePath::Paste:
      return "paste";
    case LivePath::Revert:
      return "revert";
    case LivePath::Transform:
      return "transform";
  }
  return "unknown";
}

inline LiveKind classify_live_flags(bool text_editing, bool text_nonempty, bool persistent_preview,
                                   bool drag_shape, bool floating) {
  if (text_editing) {
    return text_nonempty ? LiveKind::TextContent : LiveKind::TextEmpty;
  }
  if (persistent_preview) {
    return LiveKind::PersistentShape;
  }
  if (drag_shape) {
    return LiveKind::DragShape;
  }
  if (floating) {
    return LiveKind::Floating;
  }
  return LiveKind::None;
}

// One rule for every path. Content is stamped onto its own layer, kept, or
// asked about. It is never dropped implicitly. History moves (undo, redo,
// jump) keep the edit and do not step, so one Ctrl+Z cannot both delete the
// shape and undo the previous stroke.
inline LiveDisposition live_edit_disposition(LiveKind kind, LivePath path) {
  if (kind == LiveKind::None) {
    return LiveDisposition::None;
  }
  const bool empty_text = kind == LiveKind::TextEmpty;
  switch (path) {
    case LivePath::Undo:
    case LivePath::Redo:
    case LivePath::HistoryJump:
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Keep;
    case LivePath::LayerLock:
      return LiveDisposition::Leave;
    case LivePath::Recovery:
      return empty_text ? LiveDisposition::None : LiveDisposition::Composite;
    case LivePath::Quit:
    case LivePath::CloseTab:
    case LivePath::Revert:
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Prompt;
    case LivePath::ToolChange:
      // A float belongs to the document, not the tool. Switching to the
      // rectangle select after a paste must not stamp the paste away.
      if (kind == LiveKind::Floating) {
        return LiveDisposition::Leave;
      }
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Stamp;
    case LivePath::Open:
    case LivePath::NewDocument:
    case LivePath::TabSwitch:
    case LivePath::LayerChange:
    case LivePath::LayerAdd:
    case LivePath::LayerDelete:
    case LivePath::LayerMove:
    case LivePath::Save:
    case LivePath::Paste:
    case LivePath::Transform:
      return empty_text ? LiveDisposition::CancelEmpty : LiveDisposition::Stamp;
  }
  return LiveDisposition::Stamp;
}

// Close and quit must ask before destroying content that is not in the file
// yet, even when the history dirty bit is still clear (a float that has not
// been stamped). An empty text box is not content.
inline bool live_edit_blocks_silent_close(LiveKind kind) {
  switch (live_edit_disposition(kind, LivePath::CloseTab)) {
    case LiveDisposition::Prompt:
    case LiveDisposition::Stamp:
    case LiveDisposition::Keep:
      return true;
    case LiveDisposition::None:
    case LiveDisposition::Leave:
    case LiveDisposition::Composite:
    case LiveDisposition::CancelEmpty:
      return false;
  }
  return false;
}

LiveKind classify_live(const Tool* tool, const Document* document);

// Applies the disposition. Blocked means the path must not continue and the
// live edit is still on its own layer and document. Prompt means the caller
// asks; this function does not stamp or cancel.
SettleResult settle_tool_live(Tool* tool, Document* document, LivePath path);

// A recovery file is removed only after Recover has opened and adopted it.
inline bool recovery_file_consumed(bool opened) { return opened; }

// Open must not touch an in-progress edit until the new document will replace
// or sit beside the current one. A failed parse or a cancelled size warning
// leaves the edit alone.
inline bool settle_before_open_replaces(bool load_will_replace) { return load_will_replace; }

// Brush tips do not rewrite the shared stroke width. The width-stack mark is
// the stroke size only when that exact size is one of the rows.
inline int stroke_after_selecting_brush_tip(int stroke_size, int /*tip_size*/) { return stroke_size; }

inline int marked_line_width(int stroke_size, const int* choices, int count) {
  if (choices == nullptr) {
    return -1;
  }
  for (int i = 0; i < count; ++i) {
    if (choices[i] == stroke_size) {
      return choices[i];
    }
  }
  return -1;
}

// Fill and the wand each run the tolerance stored on that tool. A stale host
// value must not replace the number on the spin.
inline int tolerance_for_click(int tool_tolerance, int /*host_tolerance*/) {
  return tool_tolerance < 0 ? 0 : tool_tolerance;
}

}  // namespace lundukepaint

#endif
