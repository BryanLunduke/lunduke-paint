// SPDX-License-Identifier: GPL-3.0-or-later
// Round 9, without a display: a recovery fingerprint is only marked current
// after a successful write and a failed or vanished file is retried (P-1);
// background autosave steps swallow allocation failure instead of reaching
// the out-of-memory dialog (P-2).

#include "io/crash_recovery.hpp"

#include <cstdio>
#include <fstream>
#include <new>
#include <string>

#include <glib.h>
#include <unistd.h>

using namespace lundukepaint;

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
  char tmpl[] = "/tmp/lp-r9-XXXXXX";
  const char* dir = g_mkdtemp(tmpl);
  check(dir != nullptr, "temp dir");
  const std::string path = std::string(dir) + "/recovery-1.ora";

  // P-1: failed write leaves nothing current, so the next tick retries.
  crash_recovery::RecoveryTracker t;
  check(!t.is_current(1, "sig", path), "unknown document is not current");
  t.record_result(1, "sig", false);
  check(!t.is_current(1, "sig", path), "failed write is not marked current (retry)");
  { std::ofstream(path) << "x"; }
  t.record_result(1, "sig", true);
  check(t.is_current(1, "sig", path), "successful write is current");
  check(!t.is_current(1, "sig2", path), "changed picture is not current");
  t.record_result(1, "sig", false);
  check(!t.is_current(1, "sig", path), "later failure clears the marker");
  t.record_result(1, "sig", true);
  ::unlink(path.c_str());
  check(!t.is_current(1, "sig", path), "deleted recovery file is rewritten");
  { std::ofstream(path) << "x"; }
  t.record_result(1, "", true);
  check(!t.is_current(1, "", path), "synthetic content (empty sig) is never current");
  t.record_result(1, "sig", true);
  t.forget(1);
  check(!t.is_current(1, "sig", path), "forget clears the marker");
  ::unlink(path.c_str());
  ::rmdir(dir);

  // P-2: allocation failure in a background step is swallowed and reported.
  bool threw = false;
  bool ok = true;
  try {
    ok = crash_recovery::run_background_step([]() { throw std::bad_alloc(); }, "test step");
  } catch (...) {
    threw = true;
  }
  check(!threw && !ok, "bad_alloc in autosave step does not escape");
  check(crash_recovery::run_background_step([]() {}, "test step"), "normal step succeeds");

  return g_failures == 0 ? 0 : 1;
}
