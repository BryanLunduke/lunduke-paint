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
#include <map>
#include <sstream>

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

bool read_zip(const std::string& path, std::map<std::string, std::vector<std::uint8_t>>& files,
              std::string& error) {
  archive* a = archive_read_new();
  if (a == nullptr) {
    error = "Could not allocate archive reader";
    return false;
  }
  archive_read_support_format_zip(a);
  archive_read_support_filter_all(a);
  if (archive_read_open_filename(a, path.c_str(), 16384) != ARCHIVE_OK) {
    error = archive_err(a, "Could not open OpenRaster file");
    archive_read_free(a);
    return false;
  }
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
    if (name == nullptr) {
      continue;
    }
    if (archive_entry_filetype(entry) != AE_IFREG) {
      archive_read_data_skip(a);
      continue;
    }
    std::vector<std::uint8_t> data;
    const la_int64_t sz = archive_entry_size(entry);
    if (sz > 0) {
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
        data.insert(data.end(), buf, buf + n);
      }
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

bool save_ora(const std::string& path, const Document& document, std::string& error) {
  if (document.width() < 1 || document.height() < 1 || document.layers().count() < 1) {
    error = "Nothing to save";
    return false;
  }
  archive* a = archive_write_new();
  if (a == nullptr) {
    error = "Could not allocate archive writer";
    return false;
  }
  archive_write_set_format_zip(a);
  if (archive_write_open_filename(a, path.c_str()) != ARCHIVE_OK) {
    error = archive_err(a, "Could not create OpenRaster file");
    archive_write_free(a);
    return false;
  }

  archive_write_zip_set_compression_store(a);
  if (!write_zip_entry(a, "mimetype", kMimetype, std::strlen(kMimetype))) {
    error = archive_err(a, "Could not write mimetype");
    archive_write_free(a);
    return false;
  }
  archive_write_zip_set_compression_deflate(a);

  const std::string stack_xml = make_stack_xml(document);
  if (!write_zip_entry(a, "stack.xml", stack_xml.data(), stack_xml.size())) {
    error = archive_err(a, "Could not write stack.xml");
    archive_write_free(a);
    return false;
  }

  for (int i = 0; i < document.layers().count(); ++i) {
    const Layer& layer = document.layers().at(i);
    std::vector<std::uint8_t> png;
    if (!encode_png_memory(layer.pixels(), layer.width(), layer.height(), layer.stride(), png,
                           error)) {
      archive_write_free(a);
      return false;
    }
    const std::string name = "data/layer-" + std::to_string(i) + ".png";
    if (!write_zip_entry(a, name.c_str(), png.data(), png.size())) {
      error = archive_err(a, "Could not write layer PNG");
      archive_write_free(a);
      return false;
    }
  }

  std::vector<std::uint8_t> merged(
      static_cast<std::size_t>(document.width()) * static_cast<std::size_t>(document.height()) * 4,
      0);
  document.layers().composite_rect(merged.data(), document.width() * 4,
                                   Rect{0, 0, document.width(), document.height()});
  std::vector<std::uint8_t> merged_png;
  if (!encode_png_memory(merged.data(), document.width(), document.height(), document.width() * 4,
                         merged_png, error)) {
    archive_write_free(a);
    return false;
  }
  if (!write_zip_entry(a, "mergedimage.png", merged_png.data(), merged_png.size())) {
    error = archive_err(a, "Could not write mergedimage.png");
    archive_write_free(a);
    return false;
  }

  int tw = document.width();
  int th = document.height();
  if (tw > 256 || th > 256) {
    if (tw >= th) {
      th = std::max(1, th * 256 / tw);
      tw = 256;
    } else {
      tw = std::max(1, tw * 256 / th);
      th = 256;
    }
  }
  std::vector<std::uint8_t> thumb(
      static_cast<std::size_t>(tw) * static_cast<std::size_t>(th) * 4, 0);
  if (tw == document.width() && th == document.height()) {
    thumb = merged;
  } else {
    scale_bilinear(merged.data(), document.width(), document.height(), document.width() * 4,
                   thumb.data(), tw, th, tw * 4);
  }
  std::vector<std::uint8_t> thumb_png;
  if (!encode_png_memory(thumb.data(), tw, th, tw * 4, thumb_png, error)) {
    archive_write_free(a);
    return false;
  }
  if (!write_zip_entry(a, "Thumbnails/thumbnail.png", thumb_png.data(), thumb_png.size())) {
    error = archive_err(a, "Could not write Thumbnails/thumbnail.png");
    archive_write_free(a);
    return false;
  }

  if (archive_write_close(a) != ARCHIVE_OK) {
    error = archive_err(a, "Could not finish OpenRaster file");
    archive_write_free(a);
    return false;
  }
  archive_write_free(a);
  return true;
}

bool load_ora_preview_png(const std::string& path, std::vector<std::uint8_t>& png) {
  png.clear();
  archive* a = archive_read_new();
  if (a == nullptr) {
    return false;
  }
  archive_read_support_format_zip(a);
  archive_read_support_filter_all(a);
  if (archive_read_open_filename(a, path.c_str(), 16384) != ARCHIVE_OK) {
    archive_read_free(a);
    return false;
  }
  std::vector<std::uint8_t> merged;
  std::vector<std::uint8_t> thumb;
  archive_entry* entry = nullptr;
  bool failed = false;
  while (!failed) {
    const int r = archive_read_next_header(a, &entry);
    if (r == ARCHIVE_EOF) {
      break;
    }
    if (r != ARCHIVE_OK) {
      failed = true;
      break;
    }
    const char* name = archive_entry_pathname(entry);
    const bool want_merged = name != nullptr && std::strcmp(name, "mergedimage.png") == 0;
    const bool want_thumb = name != nullptr && std::strcmp(name, "Thumbnails/thumbnail.png") == 0;
    if ((!want_merged && !want_thumb) || archive_entry_filetype(entry) != AE_IFREG) {
      archive_read_data_skip(a);
      continue;
    }
    std::vector<std::uint8_t> data;
    const la_int64_t sz = archive_entry_size(entry);
    if (sz > 0 && sz < 64 * 1024 * 1024) {
      data.resize(static_cast<std::size_t>(sz));
      std::size_t got = 0;
      while (got < data.size()) {
        const la_ssize_t n = archive_read_data(a, data.data() + got, data.size() - got);
        if (n <= 0) {
          failed = true;
          break;
        }
        got += static_cast<std::size_t>(n);
      }
    } else if (sz <= 0) {
      std::uint8_t buf[4096];
      while (!failed) {
        const la_ssize_t n = archive_read_data(a, buf, sizeof(buf));
        if (n == 0) {
          break;
        }
        if (n < 0) {
          failed = true;
          break;
        }
        data.insert(data.end(), buf, buf + n);
      }
    } else {
      archive_read_data_skip(a);
      continue;
    }
    if (failed) {
      break;
    }
    if (want_merged) {
      merged = std::move(data);
    } else {
      thumb = std::move(data);
    }
  }
  archive_read_free(a);
  if (!merged.empty()) {
    png = std::move(merged);
    return true;
  }
  if (!thumb.empty()) {
    png = std::move(thumb);
    return true;
  }
  return false;
}

LoadedOra load_ora(const std::string& path) {
  LoadedOra out;
  std::map<std::string, std::vector<std::uint8_t>> files;
  if (!read_zip(path, files, out.error)) {
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
  if (nested) {
    bake_group_offsets(root, 0, 0, top_to_bottom);
    out.nested_groups = true;
    out.stack = std::move(root);
  }
  out.layers = std::move(top_to_bottom);
  return out;
}

}  // namespace lundukepaint
