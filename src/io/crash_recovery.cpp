// SPDX-License-Identifier: GPL-3.0-or-later

#include "io/crash_recovery.hpp"

#include "io/ora.hpp"

#include <glib.h>

#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace lundukepaint {
namespace crash_recovery {
namespace {

bool owned_by_user(const std::string& path) {
  std::string cur = path;
  while (!cur.empty()) {
    struct stat st;
    if (lstat(cur.c_str(), &st) == 0) {
      if (S_ISLNK(st.st_mode)) {
        return false;
      }
      return st.st_uid == geteuid();
    }
    if (cur == "/") {
      break;
    }
    const auto slash = cur.find_last_of('/');
    if (slash == std::string::npos) {
      break;
    }
    if (slash == 0) {
      cur = "/";
      continue;
    }
    cur.resize(slash);
  }
  return false;
}

}  // namespace

std::string state_dir() {
  const char* env = g_getenv("XDG_STATE_HOME");
  std::string base;
  if (env != nullptr && env[0] != '\0' && owned_by_user(env)) {
    base = env;
  } else {
    const char* home = g_get_home_dir();
    if (home == nullptr || home[0] == '\0') {
      home = ".";
    }
    base = std::string(home) + "/.local/state";
  }
  return base + "/lunduke-paint";
}

std::string autosave_path() {
  return state_dir() + "/recovery.ora";
}

bool exists() {
  struct stat st;
  if (lstat(autosave_path().c_str(), &st) != 0) {
    return false;
  }
  return S_ISREG(st.st_mode);
}

bool prepare_state_dir(std::string& error) {
  const std::string dir = state_dir();
  struct stat st;
  if (lstat(dir.c_str(), &st) == 0) {
    if (S_ISLNK(st.st_mode) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid()) {
      error = "Crash-recovery directory is not a private directory";
      return false;
    }
    if (chmod(dir.c_str(), 0700) != 0) {
      error = "Could not restrict crash-recovery directory";
      return false;
    }
    return true;
  }
  if (g_mkdir_with_parents(dir.c_str(), 0700) != 0) {
    error = "Could not create crash-recovery directory";
    return false;
  }
  if (chmod(dir.c_str(), 0700) != 0) {
    error = "Could not restrict crash-recovery directory";
    return false;
  }
  return true;
}

bool write_document(const Document& document, std::string& error) {
  if (!prepare_state_dir(error)) {
    return false;
  }
  return save_ora(autosave_path(), document, error);
}

void clear() {
  unlink(autosave_path().c_str());
}

bool clear_after_discard(bool other_documents_dirty) {
  if (other_documents_dirty) {
    return false;
  }
  clear();
  return true;
}

}  // namespace crash_recovery
}  // namespace lundukepaint
