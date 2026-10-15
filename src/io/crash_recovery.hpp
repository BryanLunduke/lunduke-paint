// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_CRASH_RECOVERY_HPP
#define LUNDUKEPAINT_IO_CRASH_RECOVERY_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
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

// P-2 (round 11): a small sidecar "<recovery file>.name" holds the document's
// user-visible name so the recovery prompt can show it instead of the
// internal file name. Empty title means "Untitled".
std::string name_path_for(const std::string& recovery_path);
// P-2 (round 14): the title written to a sidecar. A saved document uses its
// file name; an unsaved recovered one keeps its recovery label so a second
// crash does not turn it into "Untitled".
std::string sidecar_title(const std::string& document_path, const std::string& recovery_label);

bool write_name(const std::string& recovery_path, const std::string& title,
                const std::string& original_path = std::string());
std::string read_name(const std::string& recovery_path);
// Round 12: original absolute path from the sidecar ("" when unknown).
std::string read_original_path(const std::string& recovery_path);
bool has_sidecar(const std::string& recovery_path);
// Name to show for a recovery file: sidecar name, else original file name,
// else "Drawing from <date>" for legacy autosaves without a sidecar (P-2),
// else "Untitled".
std::string recovered_name(const std::string& recovery_path);
// P-3: truncate to at most max_bytes on a UTF-8 code-point boundary.
std::string truncate_utf8(const std::string& text, std::size_t max_bytes);
// "<name> — last saved <date time>" for one recovery file.
std::string describe(const std::string& recovery_path);

// P-1 (round 11): what the recovery prompt's answer means. Only an explicit
// Discard that the user then confirms deletes files; Escape, the close
// button or "Not Now" keep everything for the next launch.
enum class RecoveryChoice { Recover, Discard, Keep };
inline RecoveryChoice recovery_choice(bool pressed_recover, bool pressed_discard,
                                      bool discard_confirmed) {
  if (pressed_recover) {
    return RecoveryChoice::Recover;
  }
  if (pressed_discard && discard_confirmed) {
    return RecoveryChoice::Discard;
  }
  return RecoveryChoice::Keep;
}

// P-2 (round 13): Discard... then Cancel on "Delete unsaved work?" shows the
// recovery prompt again instead of closing it.
inline bool discard_cancel_returns_to_prompt(bool pressed_discard, bool discard_confirmed) {
  return pressed_discard && !discard_confirmed;
}

// P-1 (round 13): after Recover, rename the recovery file (and its sidecar)
// to the recovered document's own autosave path, so a crash before the next
// autosave tick still finds it. Returns false and keeps the file when the
// rename fails or either path is not a recovery path.
bool adopt_file(const std::string& from, const std::string& to);

// Cheap fingerprint of what a recovery write would contain: canvas size and
// each layer's (identity, revision) plus its properties. Equal fingerprints
// mean the last recovery file is still current, so the tick can be skipped.
std::string recovery_signature(const Document& document);

// P-1 (round 9): remembers which fingerprint is on disk per document. A
// fingerprint is recorded only after the background write succeeded; a
// failed write forgets it so the next tick retries. A recovery file that
// vanished (cleanup tools, user) is rewritten even if the fingerprint matches.
class RecoveryTracker {
 public:
  bool is_current(std::uint64_t id, const std::string& sig, const std::string& path) const;
  void record_result(std::uint64_t id, const std::string& sig, bool ok);
  void forget(std::uint64_t id);

 private:
  std::map<std::uint64_t, std::string> written_;
};

// P-2 (round 9): runs a background-autosave step and swallows allocation
// failure (logged with g_warning) so the autosave timer never shows the
// out-of-memory dialog. Returns false when the step failed; the next tick
// retries.
bool run_background_step(const std::function<void()>& step, const char* what);

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
