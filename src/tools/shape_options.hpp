// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_TOOLS_SHAPE_OPTIONS_HPP
#define LUNDUKEPAINT_TOOLS_SHAPE_OPTIONS_HPP

#include <map>
#include <string>

namespace lundukepaint {

// Outline and filled variants of one shape family share these. Pressing the
// fill-mode key (U, or R then J, and the other pairs) shows the values the
// next drag will use.
struct ShapeFamilyOptions {
  bool antialias = false;
  int corner_radius = 12;
};

inline ShapeFamilyOptions& shape_family_options(const char* family) {
  static std::map<std::string, ShapeFamilyOptions> options;
  return options[family != nullptr ? family : ""];
}

}  // namespace lundukepaint

#endif
