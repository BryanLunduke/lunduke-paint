// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_ORA_HPP
#define LUNDUKEPAINT_IO_ORA_HPP

#include "doc/layer.hpp"
#include "raster/blend.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
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
  // True when the stack must be installed (nested groups, or a root group
  // whose opacity, blend, or isolation changes the composite).
  bool has_stack = false;
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

// Each zip entry, and the sum of entries, is rejected at these caps before
// any buffer is grown to the claimed size. The unknown-size read stops at
// the same per-entry cap. Preview and full load share the entry cap.
constexpr std::size_t kMaxOraEntryBytes = 64u * 1024u * 1024u;
constexpr std::size_t kMaxOraTotalBytes = 512u * 1024u * 1024u;

std::size_t ora_max_entry_bytes();
std::size_t ora_max_total_bytes();

bool read_ora_zip(const std::string& path, std::map<std::string, std::vector<std::uint8_t>>& files,
                  std::string& error, std::size_t entry_cap, std::size_t total_cap);

bool load_ora_preview_png_limited(const std::string& path, std::vector<std::uint8_t>& png,
                                  std::size_t entry_cap);

// Appends `n` bytes unless that would pass `cap`. The unknown-size zip loop
// uses this so a missing size cannot grow without bound.
bool ora_append_capped(std::vector<std::uint8_t>& data, const std::uint8_t* bytes, std::size_t n,
                       std::size_t cap, std::string& error);

void forget_layer_png(std::uint64_t identity);
std::size_t ora_png_cache_entries();
std::size_t ora_png_cache_bytes();
void ora_debug_set_png_cache_cap(std::size_t bytes);

}  // namespace lundukepaint

#endif
