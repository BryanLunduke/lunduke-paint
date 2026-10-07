// SPDX-License-Identifier: GPL-3.0-or-later

#include "io/ora.hpp"

#include "doc/document.hpp"
#include "io/image_io.hpp"
#include "raster/blend.hpp"
#include "raster/transform.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <pugixml.hpp>

#include <algorithm>
#include <cstring>
#include <iterator>
#include <list>
#include <map>
#include <mutex>
#include <sstream>
#include <utility>

namespace lundukepaint {
namespace {

constexpr const char* kMimetype = "image/openraster";
constexpr const char* kLundukePaintNs = "http://lunduke.com/ns/lunduke-paint";

struct ZipEntry {
  std::string path;
  std::vector<std::uint8_t> data;
};

std::string archive_err(archive* a, const char* fallback) {
  const char* msg = archive_error_string(a);
  return msg != nullptr ? msg : fallback;
}

bool write_zip_entry(archive* a, const char* path, const void* data, std::size_t size) {
  archive_entry* entry = archive_entry_new();
  if (entry == nullptr) {
    return false;
  }
  archive_entry_set_pathname(entry, path);
  archive_entry_set_size(entry, static_cast<la_int64_t>(size));
  archive_entry_set_filetype(entry, AE_IFREG);
  archive_entry_set_perm(entry, 0644);
  if (archive_write_header(a, entry) != ARCHIVE_OK) {
    archive_entry_free(entry);
    return false;
  }
  if (size > 0 && archive_write_data(a, data, size) != static_cast<la_ssize_t>(size)) {
    archive_entry_free(entry);
    return false;
  }
  archive_entry_free(entry);
  return true;
}

bool read_zip_capped(const std::string& path, std::map<std::string, std::vector<std::uint8_t>>& files,
                     std::string& error, std::size_t entry_cap, std::size_t total_cap) {
  archive* a = archive_read_new();
  if (a == nullptr) {
    error = "Could not allocate archive reader";
    return false;
  }
  archive_read_support_format_zip(a);
  if (archive_read_open_filename(a, path.c_str(), 16384) != ARCHIVE_OK) {
    error = archive_err(a, "Could not open OpenRaster file");
    archive_read_free(a);
    return false;
  }
  std::size_t total = 0;
  archive_entry* entry = nullptr;
  while (true) {
    const int r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) {
      break;
    }
    if (r != ARCHIVE_OK) {
      error = archive_err(a, "Corrupt or truncated OpenRaster file");
      archive_read_free(a);
      return false;
    }
    const char* name = archive_entry_pathname(entry);
    if (name == nullptr || archive_entry_filetype(entry) != AE_IFREG) {
      archive_read_data_skip(a);
      continue;
    }
    std::vector<std::uint8_t> data;
    const la_int64_t sz = archive_entry_size(entry);
    if (sz > 0) {
      if (static_cast<std::uint64_t>(sz) > entry_cap ||
          total > total_cap || static_cast<std::size_t>(sz) > total_cap - total) {
        error = "OpenRaster entry exceeds the size limit";
        archive_read_free(a);
        return false;
      }
      data.resize(static_cast<std::size_t>(sz));
      std::size_t got = 0;
      while (got < data.size()) {
        const la_ssize_t n = archive_read_data(a, data.data() + got, data.size() - got);
        if (n <= 0) {
          error = "Corrupt or truncated OpenRaster file";
          archive_read_free(a);
          return false;
        }
        got += static_cast<std::size_t>(n);
      }
      total += data.size();
    } else {
      std::uint8_t buf[4096];
      while (true) {
        const la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
        if (n == 0) {
          break;
        }
        if (n < 0) {
          error = "Corrupt or truncated OpenRaster file";
          archive_read_free(a);
          return false;
        }
        if (!ora_append_capped(data, buf, static_cast<std::size_t>(n), entry_cap, error)) {
          archive_read_free(a);
          return false;
        }
        if (total > total_cap || data.size() > total_cap - total) {
          error = "OpenRaster file exceeds the size limit";
          archive_read_free(a);
          return false;
        }
      }
      total += data.size();
    }
    files[name] = std::move(data);
  }
  archive_read_free(a);
  return true;
}

void append_layer_xml(pugi::xml_node parent, const Document& document, int index, int ancestor_x,
                      int ancestor_y) {
  const Layer& layer = document.layers().at(index);
  auto node = parent.append_child("layer");
  node.append_attribute("name") = layer.name().c_str();
  const std::string src = "data/layer-" + std::to_string(index) + ".png";
  node.append_attribute("src") = src.c_str();
  node.append_attribute("x") = layer.offset_x() - ancestor_x;
  node.append_attribute("y") = layer.offset_y() - ancestor_y;
  node.append_attribute("opacity") = layer.opacity();
  node.append_attribute("visibility") = layer.visible() ? "visible" : "hidden";
  node.append_attribute("composite-op") = blend_mode_ora_op(layer.blend());
  node.append_attribute("lundukepaint:locked") = layer.locked() ? "true" : "false";
}

bool stack_covers_layers(const OraNode& node, int layer_count, std::vector<int>& seen) {
  if (!node.is_stack) {
    if (node.layer_index < 0 || node.layer_index >= layer_count) {
      return false;
    }
    if (seen[static_cast<std::size_t>(node.layer_index)] != 0) {
      return false;
    }
    seen[static_cast<std::size_t>(node.layer_index)] = 1;
    return true;
  }
  for (const OraNode& child : node.children) {
    if (!stack_covers_layers(child, layer_count, seen)) {
      return false;
    }
  }
  return true;
}

bool stack_is_usable(const Document& document, const OraNode& root) {
  if (!root.is_stack || document.layers().count() < 1) {
    return false;
  }
  std::vector<int> seen(static_cast<std::size_t>(document.layers().count()), 0);
  if (!stack_covers_layers(root, document.layers().count(), seen)) {
    return false;
  }
  for (int hit : seen) {
    if (hit != 1) {
      return false;
    }
  }
  return true;
}

void append_stack_xml(pugi::xml_node parent, const OraNode& node, const Document& document,
                      int ancestor_x, int ancestor_y) {
  auto stack = parent.append_child("stack");
  if (!node.name.empty()) {
    stack.append_attribute("name") = node.name.c_str();
  }
  stack.append_attribute("x") = node.x;
  stack.append_attribute("y") = node.y;
  stack.append_attribute("opacity") = node.opacity;
  stack.append_attribute("visibility") = node.visible ? "visible" : "hidden";
  stack.append_attribute("composite-op") = blend_mode_ora_op(node.blend);
  if (node.isolate) {
    stack.append_attribute("isolation") = "isolate";
  }
  const int ax = ancestor_x + node.x;
  const int ay = ancestor_y + node.y;
  for (const OraNode& child : node.children) {
    if (child.is_stack) {
      append_stack_xml(stack, child, document, ax, ay);
    } else {
      append_layer_xml(stack, document, child.layer_index, ax, ay);
    }
  }
}

std::string make_stack_xml(const Document& document) {
  pugi::xml_document xml;
  auto decl = xml.prepend_child(pugi::node_declaration);
  decl.append_attribute("version") = "1.0";
  decl.append_attribute("encoding") = "UTF-8";
  auto image = xml.append_child("image");
  image.append_attribute("version") = "0.0.3";
  image.append_attribute("w") = document.width();
  image.append_attribute("h") = document.height();
  image.append_attribute("xmlns:lundukepaint") = kLundukePaintNs;
  const OraNode* grouped = document.ora_stack();
  if (grouped != nullptr && stack_is_usable(document, *grouped)) {
    append_stack_xml(image, *grouped, document, 0, 0);
  } else {
    auto stack = image.append_child("stack");
    for (int i = document.layers().count() - 1; i >= 0; --i) {
      append_layer_xml(stack, document, i, 0, 0);
    }
  }
  std::ostringstream out;
  xml.save(out, "  ");
  return out.str();
}

bool load_layer_snapshot(const pugi::xml_node& node,
                         const std::map<std::string, std::vector<std::uint8_t>>& files,
                         LayerSnapshot& snap, std::string& error) {
  snap.name = node.attribute("name").as_string("Layer");
  snap.offset_x = node.attribute("x").as_int(0);
  snap.offset_y = node.attribute("y").as_int(0);
  snap.opacity = node.attribute("opacity").as_float(1.0f);
  const char* vis = node.attribute("visibility").as_string("visible");
  snap.visible = std::strcmp(vis, "hidden") != 0 && std::strcmp(vis, "false") != 0;
  snap.blend = blend_mode_from_ora(node.attribute("composite-op").as_string("svg:src-over"));
  const char* locked = node.attribute("locked").as_string(nullptr);
  if (locked == nullptr) {
    locked = node.attribute("lundukepaint:locked").as_string(nullptr);
  }
  if (locked == nullptr) {
    locked = node.attribute("brushpad:locked").as_string(nullptr);
  }
  if (locked == nullptr) {
    locked = "false";
  }
  snap.locked = std::strcmp(locked, "true") == 0 || std::strcmp(locked, "1") == 0;
  const char* src = node.attribute("src").as_string("");
  auto pit = files.find(src);
  if (pit == files.end()) {
    error = std::string("OpenRaster is missing layer image ") + src;
    return false;
  }
  LoadedImage png;
  if (!decode_png_memory(pit->second.data(), pit->second.size(), png)) {
    error = png.error.empty() ? "Corrupt layer PNG" : png.error;
    return false;
  }
  snap.width = png.width;
  snap.height = png.height;
  snap.pixels = std::move(png.rgba);
  return true;
}

void fill_stack_attrs(const pugi::xml_node& node, OraNode& group) {
  group.is_stack = true;
  group.name = node.attribute("name").as_string("");
  group.x = node.attribute("x").as_int(0);
  group.y = node.attribute("y").as_int(0);
  group.opacity = node.attribute("opacity").as_float(1.0f);
  const char* vis = node.attribute("visibility").as_string("visible");
  group.visible = std::strcmp(vis, "hidden") != 0 && std::strcmp(vis, "false") != 0;
  group.blend = blend_mode_from_ora(node.attribute("composite-op").as_string("svg:src-over"));
  const char* iso = node.attribute("isolation").as_string("");
  group.isolate = std::strcmp(iso, "isolate") == 0;
}

bool load_stack_children(const pugi::xml_node& stack, OraNode& parent,
                         std::vector<LayerSnapshot>& top_to_bottom,
                         const std::map<std::string, std::vector<std::uint8_t>>& files, bool& nested,
                         std::string& error) {
  for (auto node : stack.children()) {
    if (node.type() != pugi::node_element) {
      continue;
    }
    const char* name = node.name();
    if (std::strcmp(name, "layer") == 0) {
      LayerSnapshot snap;
      if (!load_layer_snapshot(node, files, snap, error)) {
        return false;
      }
      OraNode child;
      child.is_stack = false;
      child.layer_index = static_cast<int>(top_to_bottom.size());
      top_to_bottom.push_back(std::move(snap));
      parent.children.push_back(std::move(child));
    } else if (std::strcmp(name, "stack") == 0) {
      nested = true;
      OraNode group;
      fill_stack_attrs(node, group);
      if (!load_stack_children(node, group, top_to_bottom, files, nested, error)) {
        return false;
      }
      parent.children.push_back(std::move(group));
    }
  }
  return true;
}

void remap_layer_indices(OraNode& node, int count) {
  if (!node.is_stack) {
    node.layer_index = count - 1 - node.layer_index;
    return;
  }
  for (OraNode& child : node.children) {
    remap_layer_indices(child, count);
  }
}

bool group_affects_load(const OraNode& node) {
  if (!node.is_stack) {
    return false;
  }
  if (!node.visible || node.opacity < 0.999f || node.blend != BlendMode::Normal || node.isolate) {
    return true;
  }
  for (const OraNode& child : node.children) {
    if (group_affects_load(child)) {
      return true;
    }
  }
  return false;
}

void bake_group_offsets(const OraNode& node, int ancestor_x, int ancestor_y,
                        std::vector<LayerSnapshot>& layers) {
  if (node.is_stack) {
    for (const OraNode& child : node.children) {
      bake_group_offsets(child, ancestor_x + node.x, ancestor_y + node.y, layers);
    }
    return;
  }
  if (node.layer_index < 0 || node.layer_index >= static_cast<int>(layers.size())) {
    return;
  }
  LayerSnapshot& snap = layers[static_cast<std::size_t>(node.layer_index)];
  snap.offset_x += ancestor_x;
  snap.offset_y += ancestor_y;
}

}  // namespace

struct OraFileParts {
  std::string stack_xml;
  std::vector<std::vector<std::uint8_t>> layer_pngs;
  std::vector<std::uint8_t> merged_png;
  std::vector<std::uint8_t> thumb_png;
};

bool write_ora_parts(const std::string& path, const OraFileParts& parts, std::string& error) {
  AtomicFile dest;
  if (!atomic_create(path, dest, error)) {
    return false;
  }
  archive* a = archive_write_new();
  if (a == nullptr) {
    error = "Could not allocate archive writer";
    atomic_abort(dest);
    return false;
  }
  archive_write_set_format_zip(a);
  if (archive_write_open_fd(a, dest.fd) != ARCHIVE_OK) {
    error = archive_err(a, "Could not create OpenRaster file");
    archive_write_free(a);
    atomic_abort(dest);
    return false;
  }

  archive_write_zip_set_compression_store(a);
  if (!write_zip_entry(a, "mimetype", kMimetype, std::strlen(kMimetype))) {
    error = archive_err(a, "Could not write mimetype");
    archive_write_free(a);
    atomic_abort(dest);
    return false;
  }
  archive_write_zip_set_compression_deflate(a);

  if (!write_zip_entry(a, "stack.xml", parts.stack_xml.data(), parts.stack_xml.size())) {
    error = archive_err(a, "Could not write stack.xml");
    archive_write_free(a);
    atomic_abort(dest);
    return false;
  }

  for (std::size_t i = 0; i < parts.layer_pngs.size(); ++i) {
    const std::string name = "data/layer-" + std::to_string(i) + ".png";
    const std::vector<std::uint8_t>& png = parts.layer_pngs[i];
    if (!write_zip_entry(a, name.c_str(), png.data(), png.size())) {
      error = archive_err(a, "Could not write layer PNG");
      archive_write_free(a);
      atomic_abort(dest);
      return false;
    }
  }

  if (!parts.merged_png.empty()) {
    if (!write_zip_entry(a, "mergedimage.png", parts.merged_png.data(), parts.merged_png.size())) {
      error = archive_err(a, "Could not write mergedimage.png");
      archive_write_free(a);
      atomic_abort(dest);
      return false;
    }
  }
  if (!parts.thumb_png.empty()) {
    if (!write_zip_entry(a, "Thumbnails/thumbnail.png", parts.thumb_png.data(), parts.thumb_png.size())) {
      error = archive_err(a, "Could not write Thumbnails/thumbnail.png");
      archive_write_free(a);
      atomic_abort(dest);
      return false;
    }
  }

  if (archive_write_close(a) != ARCHIVE_OK) {
    error = archive_err(a, "Could not finish OpenRaster file");
    archive_write_free(a);
    atomic_abort(dest);
    return false;
  }
  archive_write_free(a);
  return atomic_commit(dest, error);
}

namespace {

struct PngCacheEntry {
  std::vector<std::uint8_t> png;
  std::list<std::pair<std::uint64_t, std::uint64_t>>::iterator lru;
};

std::mutex g_png_cache_mu;
std::list<std::pair<std::uint64_t, std::uint64_t>> g_png_lru;
std::map<std::pair<std::uint64_t, std::uint64_t>, PngCacheEntry> g_png_cache;
std::size_t g_png_bytes = 0;
std::size_t g_png_cap = 32u * 1024u * 1024u;

void png_cache_erase_locked(std::map<std::pair<std::uint64_t, std::uint64_t>, PngCacheEntry>::iterator it) {
  g_png_bytes -= it->second.png.size();
  g_png_lru.erase(it->second.lru);
  g_png_cache.erase(it);
}

void png_cache_evict_locked(std::size_t incoming) {
  while (!g_png_lru.empty() && g_png_bytes + incoming > g_png_cap) {
    const auto key = g_png_lru.front();
    const auto it = g_png_cache.find(key);
    if (it == g_png_cache.end()) {
      g_png_lru.pop_front();
      continue;
    }
    png_cache_erase_locked(it);
  }
}

std::vector<std::uint8_t> png_for_pixels(std::uint64_t identity, std::uint64_t revision,
                                        const std::uint8_t* pixels, int width, int height,
                                        int stride, std::string& error) {
  const auto key = std::make_pair(identity, revision);
  if (identity != 0) {
    std::lock_guard<std::mutex> lock(g_png_cache_mu);
    const auto it = g_png_cache.find(key);
    if (it != g_png_cache.end()) {
      g_png_lru.splice(g_png_lru.end(), g_png_lru, it->second.lru);
      return it->second.png;
    }
  }
  std::vector<std::uint8_t> png;
  if (!encode_png_memory(pixels, width, height, stride, png, error)) {
    return {};
  }
  if (identity != 0 && png.size() <= g_png_cap) {
    std::lock_guard<std::mutex> lock(g_png_cache_mu);
    png_cache_evict_locked(png.size());
    if (g_png_bytes + png.size() <= g_png_cap) {
      g_png_lru.push_back(key);
      PngCacheEntry entry;
      entry.png = png;
      entry.lru = std::prev(g_png_lru.end());
      g_png_bytes += png.size();
      g_png_cache.emplace(key, std::move(entry));
    }
  }
  return png;
}

void encode_merged(const OraSnapshot& snapshot, std::vector<std::uint8_t>& merged_png,
                   std::vector<std::uint8_t>& thumb_png, std::string& error) {
  if (!snapshot.write_merged || snapshot.width < 1 || snapshot.height < 1) {
    return;
  }
  std::vector<std::uint8_t> merged(static_cast<std::size_t>(snapshot.width) *
                                       static_cast<std::size_t>(snapshot.height) * 4,
                                   0);
  const Rect view{0, 0, snapshot.width, snapshot.height};
  for (const OraSnapshotLayer& layer : snapshot.layers) {
    if (!layer.visible || layer.pixels.empty()) {
      continue;
    }
    blend_layer_rect(merged.data(), snapshot.width, snapshot.height, snapshot.width * 4,
                     layer.pixels.data(), layer.width, layer.height, layer.stride, layer.offset_x,
                     layer.offset_y, view, layer.blend, layer.opacity);
  }
  if (!encode_png_memory(merged.data(), snapshot.width, snapshot.height, snapshot.width * 4,
                         merged_png, error)) {
    return;
  }
  int tw = snapshot.width;
  int th = snapshot.height;
  if (tw > 256 || th > 256) {
    if (tw >= th) {
      th = std::max(1, th * 256 / tw);
      tw = 256;
    } else {
      tw = std::max(1, tw * 256 / th);
      th = 256;
    }
  }
  std::vector<std::uint8_t> thumb(static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4, 0);
  if (tw == snapshot.width && th == snapshot.height) {
    thumb = merged;
  } else {
    scale_bilinear(merged.data(), snapshot.width, snapshot.height, snapshot.width * 4, thumb.data(),
                   tw, th, tw * 4);
  }
  encode_png_memory(thumb.data(), tw, th, tw * 4, thumb_png, error);
}

}  // namespace

bool save_ora(const std::string& path, const Document& document, std::string& error) {
  if (document.width() < 1 || document.height() < 1 || document.layers().count() < 1) {
    error = "Nothing to save";
    return false;
  }
  OraSnapshot snapshot = capture_ora_snapshot(document, true);
  return save_ora_snapshot(path, snapshot, error);
}

OraSnapshot capture_ora_snapshot(const Document& document, bool write_merged) {
  OraSnapshot snapshot;
  snapshot.width = document.width();
  snapshot.height = document.height();
  snapshot.write_merged = write_merged;
  snapshot.stack_xml = make_stack_xml(document);
  snapshot.layers.reserve(static_cast<std::size_t>(document.layers().count()));
  for (int i = 0; i < document.layers().count(); ++i) {
    const Layer& layer = document.layers().at(i);
    OraSnapshotLayer item;
    item.identity = layer.identity();
    item.revision = layer.revision();
    item.name = layer.name();
    item.visible = layer.visible();
    item.locked = layer.locked();
    item.opacity = layer.opacity();
    item.blend = layer.blend();
    item.offset_x = layer.offset_x();
    item.offset_y = layer.offset_y();
    item.width = layer.width();
    item.height = layer.height();
    item.stride = layer.stride();
    item.pixels.assign(layer.pixels(),
                       layer.pixels() + static_cast<std::size_t>(layer.stride()) *
                                            static_cast<std::size_t>(layer.height()));
    snapshot.layers.push_back(std::move(item));
  }
  return snapshot;
}

bool save_ora_snapshot(const std::string& path, const OraSnapshot& snapshot, std::string& error) {
  if (snapshot.width < 1 || snapshot.height < 1 || snapshot.layers.empty()) {
    error = "Nothing to save";
    return false;
  }
  OraFileParts parts;
  parts.stack_xml = snapshot.stack_xml;
  parts.layer_pngs.reserve(snapshot.layers.size());
  for (const OraSnapshotLayer& layer : snapshot.layers) {
    std::vector<std::uint8_t> png =
        png_for_pixels(layer.identity, layer.revision, layer.pixels.data(), layer.width, layer.height,
                       layer.stride, error);
    if (png.empty()) {
      if (error.empty()) {
        error = "Could not encode layer PNG";
      }
      return false;
    }
    parts.layer_pngs.push_back(std::move(png));
  }
  encode_merged(snapshot, parts.merged_png, parts.thumb_png, error);
  if (snapshot.write_merged && parts.merged_png.empty()) {
    if (error.empty()) {
      error = "Could not encode merged image";
    }
    return false;
  }
  error.clear();
  return write_ora_parts(path, parts, error);
}

bool load_ora_preview_png_limited(const std::string& path, std::vector<std::uint8_t>& png,
                                  std::size_t entry_cap) {
  png.clear();
  std::map<std::string, std::vector<std::uint8_t>> files;
  std::string error;
  if (!read_zip_capped(path, files, error, entry_cap, kMaxOraTotalBytes)) {
    return false;
  }
  const auto merged = files.find("mergedimage.png");
  if (merged != files.end() && !merged->second.empty()) {
    png = std::move(merged->second);
    return true;
  }
  const auto thumb = files.find("Thumbnails/thumbnail.png");
  if (thumb != files.end() && !thumb->second.empty()) {
    png = std::move(thumb->second);
    return true;
  }
  return false;
}

bool load_ora_preview_png(const std::string& path, std::vector<std::uint8_t>& png) {
  return load_ora_preview_png_limited(path, png, kMaxOraEntryBytes);
}

LoadedOra load_ora(const std::string& path) {
  LoadedOra out;
  std::map<std::string, std::vector<std::uint8_t>> files;
  if (!read_zip_capped(path, files, out.error, kMaxOraEntryBytes, kMaxOraTotalBytes)) {
    return out;
  }
  auto mit = files.find("mimetype");
  if (mit == files.end() ||
      std::string(mit->second.begin(), mit->second.end()) != kMimetype) {
    out.error = "Not an OpenRaster file (missing image/openraster mimetype)";
    return out;
  }
  auto sit = files.find("stack.xml");
  if (sit == files.end()) {
    out.error = "OpenRaster file is missing stack.xml";
    return out;
  }
  pugi::xml_document xml;
  const pugi::xml_parse_result parsed =
      xml.load_buffer(sit->second.data(), sit->second.size());
  if (!parsed) {
    out.error = "Corrupt stack.xml";
    return out;
  }
  auto image = xml.child("image");
  if (!image) {
    out.error = "stack.xml is missing the image element";
    return out;
  }
  out.width = image.attribute("w").as_int();
  out.height = image.attribute("h").as_int();
  if (out.width < 1 || out.height < 1) {
    out.error = "Invalid OpenRaster canvas size";
    return out;
  }
  if (out.width > kHardMaxSide || out.height > kHardMaxSide) {
    out.error = "Image is larger than 16384 on a side";
    return out;
  }
  if (out.width > kSoftMaxSide || out.height > kSoftMaxSide) {
    out.warn_size = true;
  }

  auto stack = image.child("stack");
  if (!stack) {
    out.error = "OpenRaster file has no layers";
    return out;
  }
  int layer_nodes = 0;
  const auto count_layers = [&layer_nodes](auto&& self, const pugi::xml_node& node) -> void {
    for (auto child : node.children()) {
      if (child.type() != pugi::node_element) {
        continue;
      }
      if (std::strcmp(child.name(), "layer") == 0) {
        ++layer_nodes;
      } else if (std::strcmp(child.name(), "stack") == 0) {
        self(self, child);
      }
    }
  };
  count_layers(count_layers, stack);
  if (layer_nodes > kHardMaxLayers) {
    out.error = "OpenRaster file has too many layers";
    return out;
  }
  std::vector<LayerSnapshot> top_to_bottom;
  OraNode root;
  fill_stack_attrs(stack, root);
  bool nested = false;
  if (!load_stack_children(stack, root, top_to_bottom, files, nested, out.error)) {
    return out;
  }
  if (top_to_bottom.empty()) {
    out.error = "OpenRaster file has no layers";
    return out;
  }
  if (static_cast<int>(top_to_bottom.size()) > kSoftMaxLayers) {
    out.warn_layers = true;
  }
  const int count = static_cast<int>(top_to_bottom.size());
  std::reverse(top_to_bottom.begin(), top_to_bottom.end());
  remap_layer_indices(root, count);
  // Root x/y applies even when the file has a single stack and no nested groups.
  bake_group_offsets(root, 0, 0, top_to_bottom);
  const bool keep_tree = nested || root.x != 0 || root.y != 0 || group_affects_load(root);
  if (keep_tree) {
    out.nested_groups = nested;
    out.has_stack = true;
    out.stack = std::move(root);
  }
  out.layers = std::move(top_to_bottom);
  return out;
}

bool ora_append_capped(std::vector<std::uint8_t>& data, const std::uint8_t* bytes, std::size_t n,
                       std::size_t cap, std::string& error) {
  if (bytes == nullptr && n > 0) {
    error = "OpenRaster entry exceeds the size limit";
    return false;
  }
  if (n > cap || data.size() > cap - n) {
    error = "OpenRaster entry exceeds the size limit";
    return false;
  }
  data.insert(data.end(), bytes, bytes + n);
  return true;
}

std::size_t ora_max_entry_bytes() {
  return kMaxOraEntryBytes;
}

std::size_t ora_max_total_bytes() {
  return kMaxOraTotalBytes;
}

bool read_ora_zip(const std::string& path, std::map<std::string, std::vector<std::uint8_t>>& files,
                  std::string& error, std::size_t entry_cap, std::size_t total_cap) {
  return read_zip_capped(path, files, error, entry_cap, total_cap);
}

void forget_layer_png(std::uint64_t identity) {
  if (identity == 0) {
    return;
  }
  std::lock_guard<std::mutex> lock(g_png_cache_mu);
  for (auto it = g_png_cache.begin(); it != g_png_cache.end();) {
    if (it->first.first == identity) {
      png_cache_erase_locked(it++);
    } else {
      ++it;
    }
  }
}

std::size_t ora_png_cache_entries() {
  std::lock_guard<std::mutex> lock(g_png_cache_mu);
  return g_png_cache.size();
}

std::size_t ora_png_cache_bytes() {
  std::lock_guard<std::mutex> lock(g_png_cache_mu);
  return g_png_bytes;
}

void ora_debug_set_png_cache_cap(std::size_t bytes) {
  std::lock_guard<std::mutex> lock(g_png_cache_mu);
  g_png_cap = bytes == 0 ? 1 : bytes;
  png_cache_evict_locked(0);
}

}  // namespace lundukepaint
