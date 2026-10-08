// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/history.hpp"

#include <unistd.h>

namespace lundukepaint {

std::size_t suggested_undo_bytes() {
  const long pages = sysconf(_SC_PHYS_PAGES);
  const long page = sysconf(_SC_PAGE_SIZE);
  constexpr std::size_t kCeiling = 512ull * 1024ull * 1024ull;
  if (pages <= 0 || page <= 0) {
    return kDefaultUndoBytes;
  }
  const unsigned long long ram =
      static_cast<unsigned long long>(pages) * static_cast<unsigned long long>(page);
  unsigned long long budget = ram / 8ull;
  if (budget < kDefaultUndoBytes) {
    return kDefaultUndoBytes;
  }
  if (budget > kCeiling) {
    return kCeiling;
  }
  return static_cast<std::size_t>(budget);
}

History::History(int depth) {
  set_depth(depth);
}

void History::set_depth(int depth) {
  if (depth < 1) {
    depth = 1;
  }
  if (depth > kMaxUndoDepth) {
    depth = kMaxUndoDepth;
  }
  depth_ = depth;
  trim();
}

void History::set_byte_cap(std::size_t bytes) {
  if (bytes < 1) {
    bytes = 1;
  }
  byte_cap_ = bytes;
  trim();
}

std::size_t History::memory_bytes() const {
  std::size_t total = 0;
  for (const auto& command : commands_) {
    if (command) {
      total += command->memory_bytes();
    }
  }
  return total;
}

void History::commit(Document& document, std::unique_ptr<Command> command) {
  if (!command) {
    return;
  }
  command->apply(document);
  commit_applied(std::move(command));
}

void History::commit_applied(std::unique_ptr<Command> command) {
  if (!command) {
    return;
  }
  if (index_ + 1 < static_cast<int>(commands_.size())) {
    if (saved_valid_ && saved_index_ > index_) {
      saved_valid_ = false;
    }
    commands_.erase(commands_.begin() + index_ + 1, commands_.end());
  }
  commands_.push_back(std::move(command));
  index_ = static_cast<int>(commands_.size()) - 1;
  trim();
}

bool History::can_undo() const {
  return index_ >= 0;
}

bool History::can_redo() const {
  return index_ + 1 < static_cast<int>(commands_.size());
}

Rect History::undo(Document& document) {
  if (!can_undo()) {
    return {};
  }
  Rect dirty = commands_[static_cast<std::size_t>(index_)]->dirty_rect();
  commands_[static_cast<std::size_t>(index_)]->undo(document);
  --index_;
  return dirty;
}

Rect History::redo(Document& document) {
  if (!can_redo()) {
    return {};
  }
  ++index_;
  commands_[static_cast<std::size_t>(index_)]->apply(document);
  return commands_[static_cast<std::size_t>(index_)]->dirty_rect();
}

Rect History::jump_to(Document& document, int target) {
  if (target < -1) {
    target = -1;
  }
  if (target >= static_cast<int>(commands_.size())) {
    target = static_cast<int>(commands_.size()) - 1;
  }
  Rect dirty{};
  while (index_ > target) {
    dirty = rect_union(dirty, undo(document));
  }
  while (index_ < target) {
    dirty = rect_union(dirty, redo(document));
  }
  return dirty;
}

void History::clear() {
  commands_.clear();
  index_ = -1;
  saved_valid_ = true;
  saved_index_ = -1;
  base_dropped_ = false;
  drop_notice_ = false;
}

const char* History::base_label() const {
  return base_dropped_ ? "Start of history" : "New document";
}

bool History::consume_drop_notice() {
  const bool notice = drop_notice_;
  drop_notice_ = false;
  return notice;
}

bool History::matches_saved() const {
  return saved_valid_ && saved_index_ == index_;
}

void History::mark_saved() {
  saved_valid_ = true;
  saved_index_ = index_;
}

void History::invalidate_saved() {
  saved_valid_ = false;
}

std::string History::name_at(int i) const {
  if (i < 0 || i >= static_cast<int>(commands_.size())) {
    return {};
  }
  return commands_[static_cast<std::size_t>(i)]->name();
}

void History::drop_oldest() {
  if (commands_.empty()) {
    return;
  }
  commands_.erase(commands_.begin());
  if (index_ >= 0) {
    --index_;
  }
  if (!saved_valid_) {
    return;
  }
  if (saved_index_ > 0) {
    --saved_index_;
  } else if (saved_index_ == 0) {
    // The saved command itself fell off the front of the stack.
    saved_valid_ = false;
  } else {
    // saved_index_ < 0: the base no longer matches the snapshot that was
    // marked saved. Those strokes are now baked in and cannot be undone.
    saved_valid_ = false;
  }
}

void History::trim() {
  // Always keep the newest step. A single full-canvas edit on a large
  // picture can be bigger than the budget; dropping it would make Undo a
  // no-op and bake that edit into a fake "New document".
  bool dropped = false;
  while (commands_.size() > 1) {
    const bool over_depth = static_cast<int>(commands_.size()) > depth_;
    const bool over_bytes = byte_cap_ > 0 && memory_bytes() > byte_cap_;
    if (!over_depth && !over_bytes) {
      break;
    }
    drop_oldest();
    dropped = true;
  }
  if (dropped) {
    base_dropped_ = true;
    drop_notice_ = true;
  }
}

}  // namespace lundukepaint
