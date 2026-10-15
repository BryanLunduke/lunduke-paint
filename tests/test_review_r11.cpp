// SPDX-License-Identifier: GPL-3.0-or-later
// Round 11, without a display.
// P-1: only an explicit, confirmed Discard deletes recovery files; Escape,
//      the close button and "Not Now" keep them.
// P-2: the prompt text comes from the name sidecar and the file time, never
//      from the internal recovery-<id>.ora name; clear_file removes both.

#include "doc/document.hpp"
#include "io/crash_recovery.hpp"

#include <cstdio>
#include <cstdlib>
#include <glib.h>
#include <glib/gstdio.h>
#include <string>

using namespace lundukepaint;
using crash_recovery::RecoveryChoice;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}
}  // namespace

int main() {
  char dir[] = "/tmp/lunduke-paint-r11-XXXXXX";
  if (mkdtemp(dir) == nullptr) {
    return 1;
  }
  g_setenv("XDG_STATE_HOME", dir, TRUE);

  // P-1 decision table.
  check(crash_recovery::recovery_choice(true, false, false) == RecoveryChoice::Recover, "Recover");
  check(crash_recovery::recovery_choice(false, false, false) == RecoveryChoice::Keep,
        "Escape / close / Not Now keep files");
  check(crash_recovery::recovery_choice(false, true, false) == RecoveryChoice::Keep,
        "Discard without confirmation keeps files");
  check(crash_recovery::recovery_choice(false, true, true) == RecoveryChoice::Discard,
        "confirmed Discard deletes");

  // P-2 sidecar.
  auto doc = Document::create(4, 4, Color::white(), "Background");
  const std::string path = crash_recovery::autosave_path_for(0xd9b03fcafe6c0c3aULL);
  std::string error;
  check(crash_recovery::write_document_file(*doc, path, error), "write recovery file");
  check(crash_recovery::write_name(path, "holiday-card.ora"), "write name sidecar");
  check(crash_recovery::list_recovery_files().size() == 1, "sidecar not listed as a recovery file");
  const std::string text = crash_recovery::describe(path);
  std::printf("describe: %s\n", text.c_str());
  check(text.find("holiday-card.ora") == 0, "description starts with the document name");
  check(text.find("last autosaved") != std::string::npos, "description has the autosave time");
  check(text.find("recovery-") == std::string::npos, "no internal file name");
  check(crash_recovery::write_name(path, "bad\nname"), "write name with newline");
  check(crash_recovery::read_name(path) == "bad name", "newline flattened");
  check(!crash_recovery::write_name("/tmp/elsewhere.ora", "x"), "sidecar refused outside state dir");

  const std::string untitled = crash_recovery::autosave_path_for(0x1ULL);
  check(crash_recovery::write_document_file(*doc, untitled, error), "write second recovery file");
  check(crash_recovery::describe(untitled).find("Drawing from ") == 0,
        "no sidecar -> dated label (changed in round 12, P-2)");

  crash_recovery::clear_file(path);
  crash_recovery::clear_file(untitled);
  check(!g_file_test(path.c_str(), G_FILE_TEST_EXISTS), "recovery file removed");
  check(!g_file_test(crash_recovery::name_path_for(path).c_str(), G_FILE_TEST_EXISTS),
        "sidecar removed with it");
  g_rmdir((std::string(dir) + "/lunduke-paint").c_str());
  g_rmdir(dir);
  return g_failures == 0 ? 0 : 1;
}
