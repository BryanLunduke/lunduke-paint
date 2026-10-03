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

class History {
public:
  explicit History(int depth = kDefaultUndoDepth);

  void set_depth(int depth);

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

  std::vector<std::unique_ptr<Command>> commands_;
  int index_ = -1;
  int depth_ = kDefaultUndoDepth;
  bool saved_valid_ = true;
  int saved_index_ = -1;
};

}  // namespace lundukepaint

#endif
