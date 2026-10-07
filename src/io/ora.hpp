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

// Pixel copy plus stack.xml, safe to encode on a worker. `write_merged` skips
// mergedimage.png and the thumbnail when false (large-canvas autosave).
struct OraSnapshotLayer {
  std::uint64_t identity = 0;
  std::uint64_t revision = 0;
  std::string name;
  bool visible = true;
  bool locked = false;
  float opacity = 1.0f;
  BlendMode blend = BlendMode::Normal;
  int offset_x = 0;
  int offset_y = 0;
  int width = 0;
  int height = 0;
  int stride = 0;
  std::vector<std::uint8_t> pixels;
};

struct OraSnapshot {
  int width = 0;
  int height = 0;
  std::string stack_xml;
  std::vector<OraSnapshotLayer> layers;
  bool write_merged = true;
};

OraSnapshot capture_ora_snapshot(const Document& document, bool write_merged);
bool save_ora_snapshot(const std::string& path, const OraSnapshot& snapshot, std::string& error);

// PNG bytes of mergedimage.png, or Thumbnails/thumbnail.png when the merged
// image is absent. False for anything that is not a readable OpenRaster zip
// with one of those entries. Does not report a dialog.
bool load_ora_preview_png(const std::string& path, std::vector<std::uint8_t>& png);

}  // namespace lundukepaint

#endif
