// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_APP_PREFERENCES_HPP
#define LUNDUKEPAINT_APP_PREFERENCES_HPP

#include "raster/pattern.hpp"
#include "raster/types.hpp"

#include <string>
#include <vector>

namespace lundukepaint {

class Preferences {
public:
  int default_width = kDefaultWidth;
  int default_height = kDefaultHeight;
  int undo_limit = kDefaultUndoDepth;
  Color checker_light{209, 209, 209, 255};
  Color checker_dark{158, 158, 158, 255};
  int grid_threshold = 400;
  std::vector<std::string> recent_files;
  std::vector<Pattern> custom_patterns;

  void load();
  bool save() const;
  void add_recent(const std::string& path);

  static std::string config_dir();
  static std::string config_path();
};

}  // namespace lundukepaint

#endif
