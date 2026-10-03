// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_ORA_HPP
#define LUNDUKEPAINT_IO_ORA_HPP

#include "doc/layer.hpp"
#include "raster/blend.hpp"

#include <string>
#include <vector>

namespace lundukepaint {

class Document;

// One node of an OpenRaster stack. Children are stored top-to-bottom, the
// same order as stack.xml. Layer indices are into the document's bottom-to-top
// layer list.
struct OraNode {
  bool is_stack = false;
  std::string name;
  float opacity = 1.0f;
  bool visible = true;
  BlendMode blend = BlendMode::Normal;
  int x = 0;
  int y = 0;
  bool isolate = false;
  int layer_index = -1;
  std::vector<OraNode> children;
};

struct LoadedOra {
  int width = 0;
  int height = 0;
  std::vector<LayerSnapshot> layers;  // bottom → top
  std::string error;
  bool warn_size = false;
  bool warn_layers = false;
  bool nested_groups = false;
  OraNode stack;
  bool ok() const { return error.empty() && width > 0 && height > 0 && !layers.empty(); }
};

LoadedOra load_ora(const std::string& path);
bool save_ora(const std::string& path, const Document& document, std::string& error);

}  // namespace lundukepaint

#endif
