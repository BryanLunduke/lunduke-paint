// SPDX-License-Identifier: GPL-3.0-or-later

#include "io/crash_recovery.hpp"

#include "io/ora.hpp"
#include "doc/document.hpp"

#include <glib.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <new>
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
  unlink(name_path_for(path).c_str());
}

std::string name_path_for(const std::string& recovery_path) {
  return recovery_path + ".name";
}

std::string truncate_utf8(const std::string& text, std::size_t max_bytes) {
  // P-3 (round 12): cut on a code-point boundary so a long Unicode title
  // stays valid UTF-8 (read_name rejects invalid UTF-8).
  if (text.size() <= max_bytes) {
    return text;
  }
  std::size_t cut = max_bytes;
  while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
    --cut;
  }
  std::string out = text.substr(0, cut);
  const gchar* end = nullptr;
  if (!g_utf8_validate(out.c_str(), static_cast<gssize>(out.size()), &end)) {
    out.resize(static_cast<std::size_t>(end - out.c_str()));
  }
  return out;
}

namespace {

std::string single_line(std::string text) {
  for (char& c : text) {
    if (c == '\n' || c == '\r') {
      c = ' ';
    }
  }
  return text;
}

// Sidecar layout (round 12): line 1 is the display name, optional line 2 is
// the document's original absolute path. Round-11 sidecars have only line 1.
bool read_sidecar(const std::string& recovery_path, std::string& name, std::string& orig) {
  gchar* data = nullptr;
  gsize len = 0;
  struct stat st;
  const std::string side = name_path_for(recovery_path);
  if (lstat(side.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > 8192) {
    return false;
  }
  if (!g_file_get_contents(side.c_str(), &data, &len, nullptr)) {
    return false;
  }
  std::string all(data, len);
  g_free(data);
  const std::size_t nl = all.find('\n');
  name = all.substr(0, nl);
  orig = nl == std::string::npos ? std::string() : all.substr(nl + 1);
  if (!g_utf8_validate(name.c_str(), static_cast<gssize>(name.size()), nullptr)) {
    name.clear();
  }
  if (orig.empty() || orig[0] != '/' || orig.find('\n') != std::string::npos ||
      orig.find('\0') != std::string::npos) {
    orig.clear();
  }
  return true;
}

}  // namespace

bool write_name(const std::string& recovery_path, const std::string& title,
                const std::string& original_path) {
  if (!path_is_recovery(recovery_path)) {
    return false;
  }
  std::string text = single_line(truncate_utf8(title, 512));
  if (!original_path.empty() && original_path[0] == '/' && original_path.size() < 4096 &&
      original_path.find('\n') == std::string::npos &&
      original_path.find('\r') == std::string::npos && !path_is_recovery(original_path)) {
    text += "\n";
    text += original_path;
  }
  return g_file_set_contents(name_path_for(recovery_path).c_str(), text.c_str(),
                             static_cast<gssize>(text.size()), nullptr) != FALSE;
}

std::string read_name(const std::string& recovery_path) {
  std::string name, orig;
  read_sidecar(recovery_path, name, orig);
  return name;
}

std::string read_original_path(const std::string& recovery_path) {
  std::string name, orig;
  read_sidecar(recovery_path, name, orig);
  return orig;
}

bool has_sidecar(const std::string& recovery_path) {
  std::string name, orig;
  return read_sidecar(recovery_path, name, orig);
}

std::string recovered_name(const std::string& recovery_path) {
  std::string name = read_name(recovery_path);
  if (!name.empty()) {
    return name;
  }
  const std::string orig = read_original_path(recovery_path);
  if (!orig.empty()) {
    gchar* base = g_path_get_basename(orig.c_str());
    gchar* utf8 = g_filename_display_name(base);
    std::string out = utf8 != nullptr ? utf8 : "";
    g_free(utf8);
    g_free(base);
    if (!out.empty()) {
      return out;
    }
  }
  if (!has_sidecar(recovery_path)) {
    // P-2 (round 12): pre-0.9.4-5 autosaves never wrote a sidecar.
    struct stat st;
    if (stat(recovery_path.c_str(), &st) == 0) {
      GDateTime* when = g_date_time_new_from_unix_local(static_cast<gint64>(st.st_mtime));
      if (when != nullptr) {
        gchar* stamp = g_date_time_format(when, "%b %e, %Y at %H:%M");
        g_date_time_unref(when);
        if (stamp != nullptr) {
          std::string out = std::string("Drawing from ") + stamp;
          g_free(stamp);
          return out;
        }
      }
    }
    return "Drawing from an earlier version";
  }
  return "Untitled";
}

std::string describe(const std::string& recovery_path) {
  const std::string name = recovered_name(recovery_path);
  if (!has_sidecar(recovery_path)) {
    return name;  // the legacy label already carries the date
  }
  struct stat st;
  if (stat(recovery_path.c_str(), &st) != 0) {
    return name;
  }
  GDateTime* when = g_date_time_new_from_unix_local(static_cast<gint64>(st.st_mtime));
  if (when == nullptr) {
    return name;
  }
  gchar* stamp = g_date_time_format(when, "%b %e, %Y at %H:%M");
  g_date_time_unref(when);
  std::string out = name;
  if (stamp != nullptr) {
    out += " \u2014 last autosaved ";
    out += stamp;
    g_free(stamp);
  }
  return out;
}

bool clear_after_discard(bool other_documents_dirty) {
  if (other_documents_dirty) {
    return false;
  }
  clear();
  return true;
}

std::string recovery_signature(const Document& document) {
  std::string sig = std::to_string(document.width()) + "x" + std::to_string(document.height()) +
                    "/" + std::to_string(document.layers().active_index());
  for (int i = 0; i < document.layers().count(); ++i) {
    const Layer& layer = document.layers().at(i);
    char buf[160];
    std::snprintf(buf, sizeof(buf), "|%llu:%llu:%d%d:%.4f:%d:%d,%d:",
                  static_cast<unsigned long long>(layer.identity()),
                  static_cast<unsigned long long>(layer.revision()), layer.visible() ? 1 : 0,
                  layer.locked() ? 1 : 0, static_cast<double>(layer.opacity()),
                  static_cast<int>(layer.blend()), layer.offset_x(), layer.offset_y());
    sig += buf;
    sig += layer.name();
  }
  return sig;
}

bool RecoveryTracker::is_current(std::uint64_t id, const std::string& sig,
                                 const std::string& path) const {
  const auto it = written_.find(id);
  if (it == written_.end() || it->second != sig) {
    return false;
  }
  return g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR) != FALSE;
}

void RecoveryTracker::record_result(std::uint64_t id, const std::string& sig, bool ok) {
  if (ok && !sig.empty()) {
    written_[id] = sig;
  } else {
    written_.erase(id);
  }
}

void RecoveryTracker::forget(std::uint64_t id) {
  written_.erase(id);
}

bool run_background_step(const std::function<void()>& step, const char* what) {
  try {
    step();
    return true;
  } catch (const std::bad_alloc&) {
    g_warning("lunduke-paint: %s skipped: not enough memory; will retry", what);
  } catch (const std::exception& e) {
    g_warning("lunduke-paint: %s skipped: %s; will retry", what, e.what());
  }
  return false;
}

}  // namespace crash_recovery
}  // namespace lundukepaint
