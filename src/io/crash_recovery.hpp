// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_CRASH_RECOVERY_HPP
#define LUNDUKEPAINT_IO_CRASH_RECOVERY_HPP

#include <string>

namespace lundukepaint {

class Document;

namespace crash_recovery {

std::string state_dir();
std::string autosave_path();
bool exists();
bool prepare_state_dir(std::string& error);
bool write_document(const Document& document, std::string& error);
void clear();

// Unlinks the recovery file when this discard leaves no other dirty document.
// Returns true when the file was removed.
bool clear_after_discard(bool other_documents_dirty);

// Live effect previews must not be written as the crash-recovery image.
inline bool recovery_snapshot_allowed(bool effect_preview_active) {
  return !effect_preview_active;
}

}  // namespace crash_recovery

}  // namespace lundukepaint

#endif
