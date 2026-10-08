// SPDX-License-Identifier: GPL-3.0-or-later

#include "io/crash_recovery.hpp"

#include "io/ora.hpp"

#include <glib.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

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

std::string autosave_path_for(std::uint64_t id) {
  char name[64];
  std::snprintf(name, sizeof(name), "/recovery-%llx.ora", static_cast<unsigned long long>(id));
  return state_dir() + name;
}

namespace {

bool recovery_basename(const char* name) {
  if (name == nullptr) {
    return false;
  }
  if (std::strcmp(name, "recovery.ora") == 0) {
    return true;
  }
  const char prefix[] = "recovery-";
  if (std::strncmp(name, prefix, sizeof(prefix) - 1) != 0) {
    return false;
  }
  const char* hex = name + (sizeof(prefix) - 1);
  const char* dot = std::strrchr(hex, '.');
  if (dot == nullptr || std::strcmp(dot, ".ora") != 0 || dot == hex) {
    return false;
  }
  for (const char* p = hex; p < dot; ++p) {
    if (!std::isxdigit(static_cast<unsigned char>(*p))) {
      return false;
    }
  }
  return true;
}

std::string basename_of(const std::string& path) {
  const auto slash = path.find_last_of('/');
  if (slash == std::string::npos) {
    return path;
  }
  return path.substr(slash + 1);
}

}  // namespace

bool path_is_recovery(const std::string& path) {
  if (!recovery_basename(basename_of(path).c_str())) {
    return false;
  }
  const std::string dir = state_dir();
  return path == dir + "/" + basename_of(path);
}

std::vector<std::string> list_recovery_files() {
  std::vector<std::string> out;
  const std::string dir = state_dir();
  GDir* handle = g_dir_open(dir.c_str(), 0, nullptr);
  if (handle == nullptr) {
    return out;
  }
  const gchar* name = nullptr;
  while ((name = g_dir_read_name(handle)) != nullptr) {
    if (!recovery_basename(name)) {
      continue;
    }
    const std::string path = dir + "/" + name;
    struct stat st;
    if (lstat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) {
      continue;
    }
    out.push_back(path);
  }
  g_dir_close(handle);
  return out;
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

bool write_document_file(const Document& document, const std::string& path, std::string& error) {
  if (!path_is_recovery(path)) {
    error = "Refusing to write crash recovery outside its directory";
    return false;
  }
  if (!prepare_state_dir(error)) {
    return false;
  }
  return save_ora(path, document, error);
}

bool write_document(const Document& document, std::string& error) {
  return write_document_file(document, autosave_path(), error);
}

void clear() {
  unlink(autosave_path().c_str());
}

void clear_file(const std::string& path) {
  if (!path_is_recovery(path)) {
    return;
  }
  unlink(path.c_str());
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
