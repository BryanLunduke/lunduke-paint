// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_DOC_HISTORY_HPP
#define LUNDUKEPAINT_DOC_HISTORY_HPP

#include "doc/command.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace lundukepaint {

class Document;

// Shown once in the status line, and kept on the History panel, when trim
// drops steps the user can no longer restore.
inline constexpr const char* kUndoDroppedNotice =
    "Older undo steps were dropped to save memory";

// At least 256 MB, at most 512 MB, one eighth of physical RAM in between.
// The History object itself still starts at kDefaultUndoBytes so a caller
// can set a tighter cap; the window applies this budget to each document.
std::size_t suggested_undo_bytes();

class History {
public:
  explicit History(int depth = kDefaultUndoDepth);

  void set_depth(int depth);
  void set_byte_cap(std::size_t bytes);
  std::size_t byte_cap() const { return byte_cap_; }
  std::size_t memory_bytes() const;

  // False after the oldest steps have been discarded. The pixels at index -1
  // are then the oldest restorable state, not the original new document.
  bool base_dropped() const { return base_dropped_; }
  const char* base_label() const;
  // True once after a trim drops steps. Cleared when read.
  bool consume_drop_notice();

  // Applies the command, then records it. Drops redo branch.
  void commit(Document& document, std::unique_ptr<Command> command);

  // Records an already-applied command (dirty-rect already written).
  void commit_applied(std::unique_ptr<Command> command);

  bool can_undo() const;
  bool can_redo() const;

  Rect undo(Document& document);
  Rect redo(Document& document);

  // Undo/redo until index() == target. target -1 is the initial document.
  Rect jump_to(Document& document, int target);

  void clear();

  int depth() const { return depth_; }
  int index() const { return index_; }
  int count() const { return static_cast<int>(commands_.size()); }
  std::string name_at(int i) const;

  // True when the current index is the document state last marked saved.
  bool matches_saved() const;
  void mark_saved();
  void invalidate_saved();

private:
  void trim();
  void drop_oldest();

  std::vector<std::unique_ptr<Command>> commands_;
  int index_ = -1;
  int depth_ = kDefaultUndoDepth;
  std::size_t byte_cap_ = kDefaultUndoBytes;
  bool saved_valid_ = true;
  int saved_index_ = -1;
  bool base_dropped_ = false;
  bool drop_notice_ = false;
};

}  // namespace lundukepaint

#endif
