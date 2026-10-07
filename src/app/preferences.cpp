// SPDX-License-Identifier: GPL-3.0-or-later

#include "app/preferences.hpp"

#include <algorithm>
#include <cstdio>
#include <glib.h>
#include <glib/gstdio.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>

namespace lundukepaint {
namespace {

std::string color_to_hex(Color c) {
  char buf[8];
  std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
  return buf;
}

bool parse_hex_color(const std::string& text, Color& out) {
  if (text.size() != 7 || text[0] != '#') {
    return false;
  }
  unsigned r = 0;
  unsigned g = 0;
  unsigned b = 0;
  if (std::sscanf(text.c_str(), "#%02x%02x%02x", &r, &g, &b) != 3) {
    return false;
  }
  out = Color{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g),
              static_cast<std::uint8_t>(b), 255};
  return true;
}

int clamp_int(int v, int lo, int hi) {
  return std::max(lo, std::min(hi, v));
}

}  // namespace

std::string Preferences::config_dir() {
  return Glib::build_filename(Glib::get_user_config_dir(), "lunduke-paint");
}

std::string Preferences::config_path() {
  return Glib::build_filename(config_dir(), "lunduke-paint.ini");
}

void Preferences::load() {
  Glib::KeyFile key;
  try {
    key.load_from_file(config_path());
  } catch (const Glib::Error&) {
    // One-shot migration from the legacy config filename.
    try {
      const std::string legacy =
          Glib::build_filename(config_dir(), "brushpad.ini");
      key.load_from_file(legacy);
      try {
        key.save_to_file(config_path());
      } catch (const Glib::Error&) {
      }
    } catch (const Glib::Error&) {
      return;
    }
  }

  try {
    if (key.has_key("document", "default_width")) {
      default_width = clamp_int(key.get_integer("document", "default_width"), 1, kHardMaxSide);
    }
    if (key.has_key("document", "default_height")) {
      default_height = clamp_int(key.get_integer("document", "default_height"), 1, kHardMaxSide);
    }
  } catch (const Glib::KeyFileError&) {
  }

  try {
    if (key.has_key("history", "undo_limit")) {
      undo_limit = clamp_int(key.get_integer("history", "undo_limit"), 1, kMaxUndoDepth);
    }
  } catch (const Glib::KeyFileError&) {
  }

  try {
    if (key.has_key("view", "checker_light")) {
      Color c = checker_light;
      if (parse_hex_color(key.get_string("view", "checker_light"), c)) {
        checker_light = c;
      }
    }
    if (key.has_key("view", "checker_dark")) {
      Color c = checker_dark;
      if (parse_hex_color(key.get_string("view", "checker_dark"), c)) {
        checker_dark = c;
      }
    }
    if (key.has_key("view", "grid_threshold")) {
      grid_threshold = clamp_int(key.get_integer("view", "grid_threshold"), 100, 1600);
    }
  } catch (const Glib::KeyFileError&) {
  }

  try {
    if (key.has_key("recent", "files")) {
      recent_files.clear();
      for (const auto& item : key.get_string_list("recent", "files")) {
        if (!item.empty()) {
          recent_files.push_back(item);
        }
      }
    }
  } catch (const Glib::KeyFileError&) {
  }
}

void Preferences::add_recent(const std::string& path) {
  if (path.empty() || path[0] != '/') {
    return;
  }
  recent_files.erase(std::remove(recent_files.begin(), recent_files.end(), path), recent_files.end());
  recent_files.insert(recent_files.begin(), path);
  constexpr std::size_t kMaxRecent = 10;
  if (recent_files.size() > kMaxRecent) {
    recent_files.resize(kMaxRecent);
  }
  save();
}

bool Preferences::save() const {
  Glib::KeyFile key;
  try {
    key.load_from_file(config_path());
  } catch (const Glib::Error&) {
  }
  key.set_integer("document", "default_width", default_width);
  key.set_integer("document", "default_height", default_height);
  key.set_integer("history", "undo_limit", undo_limit);
  key.set_string("view", "checker_light", color_to_hex(checker_light));
  key.set_string("view", "checker_dark", color_to_hex(checker_dark));
  key.set_integer("view", "grid_threshold", grid_threshold);
  if (!recent_files.empty()) {
    std::vector<Glib::ustring> list;
    list.reserve(recent_files.size());
    for (const auto& item : recent_files) {
      list.emplace_back(item);
    }
    key.set_string_list("recent", "files", list);
  } else if (key.has_group("recent")) {
    key.remove_group("recent");
  }

  g_mkdir_with_parents(config_dir().c_str(), 0700);
  g_chmod(config_dir().c_str(), 0700);
  try {
    key.save_to_file(config_path());
    return true;
  } catch (const Glib::FileError&) {
    return false;
  }
}

}  // namespace lundukepaint
