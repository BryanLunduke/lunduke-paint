// SPDX-License-Identifier: GPL-3.0-or-later
// Round 12, without a display.
// P-1: the sidecar keeps the original absolute path so Recover restores the
//      document's real name and path (title, status bar, Save target).
// P-2: autosaves from before 0.9.4-5 (no sidecar) get a dated label.
// P-3: long titles are cut on a UTF-8 code-point boundary.

#include "doc/document.hpp"
#include "io/crash_recovery.hpp"

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
bool valid(const std::string& s) {
  return g_utf8_validate(s.c_str(), static_cast<gssize>(s.size()), nullptr) != FALSE;
}
}  // namespace

int main() {
  char dir[] = "/tmp/lunduke-paint-r12-XXXXXX";
  if (mkdtemp(dir) == nullptr) {
    return 1;
  }
  g_setenv("XDG_STATE_HOME", dir, TRUE);
  auto doc = Document::create(4, 4, Color::white(), "Background");
  std::string error;

  // P-1
  const std::string name = "\u00dcn\u00efc\u00f8d\u00e9 \u57ce\u5e02 \U0001F3D9 photo.png";
  const std::string orig = std::string("/home/u/Pictures/") + name;
  const std::string rec = crash_recovery::autosave_path_for(0xabcULL);
  check(crash_recovery::write_document_file(*doc, rec, error), "write recovery file");
  check(crash_recovery::write_name(rec, name, orig), "write sidecar with path");
  check(crash_recovery::read_name(rec) == name, "name round-trips");
  check(crash_recovery::read_original_path(rec) == orig, "original path round-trips");
  check(crash_recovery::recovered_name(rec) == name, "recovered name is the real name");
  check(crash_recovery::describe(rec).find("recovery-") == std::string::npos, "no internal name");
  check(crash_recovery::write_name(rec, "x", "relative/path.png"), "write with relative path");
  check(crash_recovery::read_original_path(rec).empty(), "relative path not stored");
  check(crash_recovery::write_name(rec, "x", "/a\nb.png"), "write with newline path");
  check(crash_recovery::read_original_path(rec).empty(), "newline path not stored");
  check(crash_recovery::write_name(rec, "", ""), "untitled sidecar");
  check(crash_recovery::recovered_name(rec) == "Untitled", "untitled stays Untitled");
  // Round-11 sidecar (name only) still reads.
  g_file_set_contents(crash_recovery::name_path_for(rec).c_str(), "old.ora", -1, nullptr);
  check(crash_recovery::read_name(rec) == "old.ora" && crash_recovery::read_original_path(rec).empty(),
        "round-11 sidecar compatible");
  crash_recovery::clear_file(rec);

  // P-2
  const std::string legacy = crash_recovery::autosave_path_for(0x1ULL);
  check(crash_recovery::write_document_file(*doc, legacy, error), "write legacy autosave");
  const std::string lab = crash_recovery::describe(legacy);
  std::printf("legacy label: %s\n", lab.c_str());
  check(lab.find("Drawing from ") == 0, "legacy autosave gets a dated label");
  check(lab.find("Untitled") == std::string::npos, "legacy label is not Untitled");
  crash_recovery::clear_file(legacy);

  // P-3
  std::string longt;
  while (longt.size() < 600) {
    longt += "\u57ce";  // 3 bytes; 512 is not a multiple of 3
  }
  const std::string cut = crash_recovery::truncate_utf8(longt, 512);
  check(cut.size() == 510 && valid(cut), "cut on a code point boundary");
  std::string emoji;
  while (emoji.size() < 600) {
    emoji += "\U0001F3D9";
  }
  check(valid(crash_recovery::truncate_utf8(emoji, 511)), "4-byte sequences not split");
  check(crash_recovery::truncate_utf8("abc", 512) == "abc", "short title unchanged");
  const std::string rec2 = crash_recovery::autosave_path_for(0xdefULL);
  check(crash_recovery::write_document_file(*doc, rec2, error), "write recovery file 2");
  check(crash_recovery::write_name(rec2, longt), "write long Unicode title");
  const std::string back = crash_recovery::read_name(rec2);
  check(!back.empty() && valid(back) && longt.compare(0, back.size(), back) == 0,
        "long Unicode title survives (not Untitled)");
  check(crash_recovery::recovered_name(rec2) != "Untitled", "long title not shown as Untitled");
  crash_recovery::clear_file(rec2);

  g_rmdir((std::string(dir) + "/lunduke-paint").c_str());
  g_rmdir(dir);
  return g_failures == 0 ? 0 : 1;
}
