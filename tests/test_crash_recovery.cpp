// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/preferences.hpp"
#include "doc/document.hpp"
#include "io/crash_recovery.hpp"

#include <cstdio>
#include <cstdlib>
#include <glib.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::crash_recovery::autosave_path;
using lundukepaint::crash_recovery::clear;
using lundukepaint::crash_recovery::clear_after_discard;
using lundukepaint::crash_recovery::exists;
using lundukepaint::crash_recovery::state_dir;
using lundukepaint::crash_recovery::write_document;

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_crash_recovery: %s\n", msg);
    return 1;
  }
  return 0;
}

}  // namespace

int main() {
  char dir[] = "/tmp/lunduke-paint-state-XXXXXX";
  if (mkdtemp(dir) == nullptr) {
    std::fprintf(stderr, "test_crash_recovery: mkdtemp failed\n");
    return 1;
  }
  g_setenv("XDG_STATE_HOME", dir, TRUE);

  int errors = 0;
  errors += expect(!exists(), "no recovery file yet");

  auto doc = Document::create(4, 3, Color::white(), "Background");
  doc->layers().active_layer().set_pixel(1, 1, Color{255, 0, 0, 255});
  std::string error;
  errors += expect(write_document(*doc, error), "write recovery");
  if (!error.empty()) {
    std::fprintf(stderr, "test_crash_recovery: %s\n", error.c_str());
  }
  errors += expect(exists(), "recovery file exists");
  errors += expect(g_file_test(autosave_path().c_str(), G_FILE_TEST_IS_REGULAR) == TRUE,
                   "path is a regular file");

  clear();
  errors += expect(!exists(), "cleared");

  errors += expect(write_document(*doc, error), "rewrite recovery");
  struct stat st;
  errors += expect(stat(state_dir().c_str(), &st) == 0, "state dir exists");
  errors += expect(S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700, "recovery dir is mode 0700");

  errors += expect(!clear_after_discard(true), "other dirty documents keep the autosave");
  errors += expect(exists(), "autosave remains while another document is dirty");
  errors += expect(clear_after_discard(false), "last discard unlinks the autosave");
  errors += expect(!exists(), "autosave is gone after the last discard");

  const std::string link_target = std::string(dir) + "/other.ora";
  {
    FILE* target = std::fopen(link_target.c_str(), "wb");
    errors += expect(target != nullptr, "symlink target");
    if (target != nullptr) {
      std::fputs("not-recovery", target);
      std::fclose(target);
    }
  }
  errors += expect(symlink(link_target.c_str(), autosave_path().c_str()) == 0, "plant a symlink");
  errors += expect(!exists(), "a symlink is not a recovery file");
  unlink(autosave_path().c_str());
  unlink(link_target.c_str());

  struct stat tmp_st;
  if (lstat("/tmp", &tmp_st) == 0 && tmp_st.st_uid != geteuid()) {
    g_setenv("XDG_STATE_HOME", "/tmp", TRUE);
    const std::string ignored = state_dir();
    errors += expect(ignored.find("/tmp/") == std::string::npos,
                     "XDG_STATE_HOME=/tmp is ignored when /tmp is not owned by the user");
  }

  rmdir(dir);

  char config_dir[] = "/tmp/lunduke-paint-config-XXXXXX";
  if (mkdtemp(config_dir) == nullptr) {
    std::fprintf(stderr, "test_crash_recovery: config mkdtemp failed\n");
    return 1;
  }
  g_setenv("XDG_CONFIG_HOME", config_dir, TRUE);
  lundukepaint::Preferences prefs;
  errors += expect(prefs.save(), "save preferences");
  const std::string saved_dir = lundukepaint::Preferences::config_dir();
  errors += expect(stat(saved_dir.c_str(), &st) == 0, "config dir exists");
  errors += expect(S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0700, "config dir is mode 0700");
  unlink((saved_dir + "/lunduke-paint.ini").c_str());
  rmdir(saved_dir.c_str());
  rmdir(config_dir);

  if (errors != 0) {
    std::fprintf(stderr, "test_crash_recovery: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_crash_recovery: ok\n");
  return 0;
}
