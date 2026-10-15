// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_SAVE_ERROR_HPP
#define LUNDUKEPAINT_IO_SAVE_ERROR_HPP

#include <string>

namespace lundukepaint {

// P-3 (round 13): save errors name the file and its folder.
inline std::string save_error_title(const std::string& path) {
  const std::size_t slash = path.find_last_of('/');
  const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
  return "Could not save \u201c" + name + "\u201d.";
}

inline std::string save_error_detail(const std::string& path, const std::string& error) {
  const std::size_t slash = path.find_last_of('/');
  std::string folder = slash == std::string::npos ? std::string(".") : path.substr(0, slash);
  if (folder.empty()) {
    folder = "/";
  }
  std::string text = "Folder: " + folder;
  if (!error.empty()) {
    text += "\n" + error;
  }
  return text;
}

}  // namespace lundukepaint

#endif
