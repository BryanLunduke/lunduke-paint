// SPDX-License-Identifier: GPL-3.0-or-later
// Round 13, without a display.
// P-1: after Recover the recovery file is kept (renamed to the document's own
//      autosave path with its sidecar) until a fresh autosave or a save.
// P-2: Discard... then Cancel goes back to the recovery prompt.
// P-3: save errors name the file and folder.

#include "doc/document.hpp"
#include "io/crash_recovery.hpp"
#include "io/save_error.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <glib.h>
#include <glib/gstdio.h>
#include <string>

using namespace lundukepaint;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}
bool exists(const std::string& p) { return g_file_test(p.c_str(), G_FILE_TEST_EXISTS) != FALSE; }
}  // namespace

int main() {
  char dir[] = "/tmp/lunduke-paint-r13-XXXXXX";
  if (mkdtemp(dir) == nullptr) {
    return 1;
  }
  g_setenv("XDG_STATE_HOME", dir, TRUE);
  auto doc = Document::create(4, 4, Color::white(), "Background");
  std::string error;

  // P-1
  const std::string old_rec = crash_recovery::autosave_path_for(0x111ULL);
  const std::string new_rec = crash_recovery::autosave_path_for(doc->recovery_id());
  check(crash_recovery::write_document_file(*doc, old_rec, error), "write old recovery file");
  check(crash_recovery::write_name(old_rec, "photo.png", "/home/u/photo.png"), "write sidecar");
  check(crash_recovery::adopt_file(old_rec, new_rec), "adopt after Recover");
  check(!exists(old_rec) && !exists(crash_recovery::name_path_for(old_rec)), "old name gone");
  check(exists(new_rec), "recovered work still on disk (survives kill -9 before next tick)");
  check(crash_recovery::read_original_path(new_rec) == "/home/u/photo.png", "sidecar moved too");
  const auto listed = crash_recovery::list_recovery_files();
  check(std::find(listed.begin(), listed.end(), new_rec) != listed.end(),
        "next launch offers it again");
  check(crash_recovery::write_document_file(*doc, new_rec, error), "fresh autosave replaces it");
  check(crash_recovery::list_recovery_files().size() == 1, "no duplicate after fresh autosave");
  check(!crash_recovery::adopt_file("/etc/passwd", new_rec), "refuses non-recovery source");
  check(!crash_recovery::adopt_file(new_rec, std::string(dir) + "/elsewhere.ora"),
        "refuses non-recovery destination");
  check(exists(new_rec), "refused adopt keeps the file");
  check(crash_recovery::adopt_file(new_rec, new_rec), "adopt onto itself is a no-op");
  crash_recovery::clear_file(new_rec);  // what Save does
  check(crash_recovery::list_recovery_files().empty(), "save clears it");

  // P-2
  check(crash_recovery::discard_cancel_returns_to_prompt(true, false), "Discard+Cancel loops");
  check(!crash_recovery::discard_cancel_returns_to_prompt(true, true), "Discard+Delete exits");
  check(!crash_recovery::discard_cancel_returns_to_prompt(false, false), "Not Now exits");

  // P-3
  const std::string p = "/tmp/r13/full/\u57ce\u5e02 big.png";
  check(save_error_title(p) == "Could not save \u201c\u57ce\u5e02 big.png\u201d.", "title names file");
  const std::string d = save_error_detail(p, "Could not write the temporary file: No space left on device");
  check(d.find("Folder: /tmp/r13/full") == 0 && d.find("No space left") != std::string::npos,
        "detail names folder and reason");
  check(save_error_detail("/x.png", "e").find("Folder: /\n") == 0, "root folder");

  g_unsetenv("XDG_STATE_HOME");
  std::string cmd = std::string("rm -rf ") + dir;
  if (std::system(cmd.c_str()) != 0) {
  }
  std::printf("%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
