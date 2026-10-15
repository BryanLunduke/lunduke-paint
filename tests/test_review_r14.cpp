// SPDX-License-Identifier: GPL-3.0-or-later
// Round 14, without a display.
// P-1: a 38 KB .ora whose layers decode to ~10 GB is refused from the PNG
//      headers before any layer is decoded (memory stays flat).
// P-2: an old-style (legacy) recovered drawing keeps its "Drawing from ..."
//      label in the sidecar, so a second crash does not show "Untitled".

#include "doc/document.hpp"
#include "io/crash_recovery.hpp"
#include "io/ora.hpp"

#include <cstdio>
#include <cstdlib>
#include <glib.h>
#include <string>
#include <sys/resource.h>

using namespace lundukepaint;

namespace {
int g_failures = 0;
void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}
long max_rss_kb() {
  struct rusage ru {};
  getrusage(RUSAGE_SELF, &ru);
  return ru.ru_maxrss;
}
}  // namespace

int main() {
  char dir[] = "/tmp/lunduke-paint-r14-XXXXXX";
  if (mkdtemp(dir) == nullptr) {
    return 1;
  }
  g_setenv("XDG_STATE_HOME", dir, TRUE);

  // P-1
  const std::uint64_t budget = ora_decode_budget_bytes();
  check(budget >= (1ull << 30) && budget <= (4ull << 30), "budget between 1 and 4 GiB");
  const long before = max_rss_kb();
  const LoadedOra bomb = load_ora(REVIEW_R14_SOURCE "/tests/data/r14-bomb.ora");
  const long after = max_rss_kb();
  std::printf("bomb: error='%s' maxrss %ld KB -> %ld KB\n", bomb.error.c_str(), before, after);
  check(!bomb.ok() && bomb.error.find("more memory") != std::string::npos, "bomb refused with memory reason");
  check(after - before < 64 * 1024, "bomb refused without decoding (RSS grew < 64 MB)");

  auto doc = Document::create(64, 48, Color::white(), "Background");
  std::string error;
  const std::string small = std::string(dir) + "/small.ora";
  check(save_ora(small, *doc, error), "save small ora");
  const LoadedOra ok = load_ora(small);
  check(ok.ok() && ok.width == 64 && ok.height == 48, "normal ora still opens");
  const LoadedOra tight = load_ora(small, 64ull * 48ull * 4ull - 1ull);
  check(!tight.ok() && tight.layers.empty(), "budget is enforced for all layers combined");
  const LoadedOra exact = load_ora(small, 64ull * 48ull * 4ull);
  check(exact.ok(), "exact budget fits");

  // P-2
  check(crash_recovery::sidecar_title("", "Drawing from Oct 14, 2026 at 22:46") ==
            "Drawing from Oct 14, 2026 at 22:46",
        "unsaved recovered drawing keeps its label");
  check(crash_recovery::sidecar_title("/home/u/cat.png", "Drawing from x") == "cat.png",
        "saved document uses its file name");
  check(crash_recovery::sidecar_title("", "").empty(), "new drawing stays untitled");
  const std::string rec = crash_recovery::autosave_path_for(doc->recovery_id());
  check(crash_recovery::write_document_file(*doc, rec, error), "write autosave");
  doc->set_recovery_label("Drawing from Oct 14, 2026 at 22:46");
  check(crash_recovery::write_name(rec, crash_recovery::sidecar_title(doc->path(), doc->recovery_label()),
                                   doc->path()),
        "write sidecar for recovered legacy drawing");
  const std::string described = crash_recovery::describe(rec);
  std::printf("second-crash prompt: %s\n", described.c_str());
  check(described.find("Drawing from Oct 14, 2026 at 22:46") == 0, "label survives a second crash");
  check(described.find("Untitled") == std::string::npos, "not Untitled");
  crash_recovery::clear_file(rec);

  g_unsetenv("XDG_STATE_HOME");
  std::string cmd = std::string("rm -rf ") + dir;
  if (std::system(cmd.c_str()) != 0) {
  }
  std::printf("%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
