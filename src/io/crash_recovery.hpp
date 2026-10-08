// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_CRASH_RECOVERY_HPP
#define LUNDUKEPAINT_IO_CRASH_RECOVERY_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace lundukepaint {

class Document;

namespace crash_recovery {

std::string state_dir();
std::string autosave_path();
// One file per document. `id` is Document::recovery_id().
std::string autosave_path_for(std::uint64_t id);
bool path_is_recovery(const std::string& path);
// Legacy recovery.ora plus every recovery-<id>.ora in the state directory.
std::vector<std::string> list_recovery_files();
bool exists();
bool prepare_state_dir(std::string& error);
bool write_document(const Document& document, std::string& error);
bool write_document_file(const Document& document, const std::string& path, std::string& error);
void clear();
// Unlinks one recovery file. Ignores anything that is not a recovery path.
void clear_file(const std::string& path);

// Unlinks the recovery file when this discard leaves no other dirty document.
// Returns true when the file was removed.
bool clear_after_discard(bool other_documents_dirty);

// Live effect previews must not be written as the crash-recovery image.
// The tick still runs: the caller substitutes the pre-preview pixels.
inline bool recovery_snapshot_allowed(bool effect_preview_active) {
  return !effect_preview_active;
}

// A stroke, text box, float, or effect preview must not skip autosave of
// pixels that are already committed. The preview itself is excluded by
// recovery_snapshot_allowed(); the tick still runs.
inline bool recovery_tick_should_run(bool document_dirty, bool effect_preview_active,
                                    bool stroke_open, bool text_open, bool floating) {
  (void)effect_preview_active;
  (void)stroke_open;
  (void)text_open;
  (void)floating;
  return document_dirty;
}

}  // namespace crash_recovery

}  // namespace lundukepaint

#endif
