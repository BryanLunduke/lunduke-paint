// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/document.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"
#include "raster/blend.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <archive.h>
#include <archive_entry.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using lundukepaint::BlendMode;
using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::Layer;
using lundukepaint::LoadedOra;
using lundukepaint::load_ora;
using lundukepaint::save_ora;

int expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_ora: %s\n", msg);
    return 1;
  }
  return 0;
}

std::string temp_ora_path() {
  char path[] = "/tmp/lunduke-paint-ora-XXXXXX";
  const int fd = mkstemp(path);
  if (fd >= 0) {
    close(fd);
    unlink(path);
  }
  return std::string(path) + ".ora";
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
  const bool ok = archive_write_header(a, entry) == ARCHIVE_OK &&
                  (size == 0 || archive_write_data(a, data, size) == static_cast<la_ssize_t>(size));
  archive_entry_free(entry);
  return ok;
}

void append_u16(std::vector<std::uint8_t>& out, unsigned value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

void append_u32(std::vector<std::uint8_t>& out, unsigned value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 16) & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 24) & 0xff));
}

// Local header only, with the data-descriptor flag and sizes left at zero.
// libarchive then reports size 0 and still returns the payload bytes, which
// is the unknown-size read the cap has to stop.
std::vector<std::uint8_t> unknown_size_zip(const std::string& name, const std::vector<std::uint8_t>& payload) {
  std::vector<std::uint8_t> out;
  append_u32(out, 0x04034b50);
  append_u16(out, 20);
  append_u16(out, 0x0008);
  append_u16(out, 0);
  append_u16(out, 0);
  append_u16(out, 0);
  append_u32(out, 0);
  append_u32(out, 0);
  append_u32(out, 0);
  append_u16(out, static_cast<unsigned>(name.size()));
  append_u16(out, 0);
  out.insert(out.end(), name.begin(), name.end());
  out.insert(out.end(), payload.begin(), payload.end());
  append_u32(out, 0x08074b50);
  append_u32(out, 0);
  append_u32(out, static_cast<unsigned>(payload.size()));
  append_u32(out, static_cast<unsigned>(payload.size()));
  return out;
}

std::vector<std::uint8_t> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), {});
}

int count_named(const lundukepaint::OraNode& node, const char* name) {
  int n = node.name == name ? 1 : 0;
  for (const auto& child : node.children) {
    n += count_named(child, name);
  }
  return n;
}

}  // namespace

int main() {
  int errors = 0;
  auto doc = Document::create(8, 6, Color::white(), "Background");
  doc->add_layer();
  Layer& top = doc->layers().active_layer();
  top.set_name("Overlay");
  top.set_opacity(0.5f);
  top.set_visible(false);
  top.set_blend(BlendMode::Multiply);
  top.set_pixel(1, 1, Color{255, 0, 0, 255});
  doc->layers().at(0).set_pixel(0, 0, Color{0, 0, 255, 255});

  const std::string path = temp_ora_path();
  std::string error;
  errors += expect(save_ora(path, *doc, error), "save_ora succeeded");
  if (!error.empty()) {
    std::fprintf(stderr, "test_ora: save error: %s\n", error.c_str());
  }

  LoadedOra loaded = load_ora(path);
  errors += expect(loaded.ok(), "load_ora succeeded");
  if (!loaded.ok()) {
    std::fprintf(stderr, "test_ora: load error: %s\n", loaded.error.c_str());
  }
  errors += expect(loaded.width == 8 && loaded.height == 6, "size");
  errors += expect(static_cast<int>(loaded.layers.size()) == 2, "two layers");
  if (loaded.layers.size() == 2) {
    errors += expect(loaded.layers[0].name == "Background", "bottom name");
    errors += expect(loaded.layers[1].name == "Overlay", "top name");
    errors += expect(loaded.layers[1].opacity > 0.49f && loaded.layers[1].opacity < 0.51f,
                     "top opacity");
    errors += expect(loaded.layers[1].visible == false, "top hidden");
    errors += expect(loaded.layers[1].blend == BlendMode::Multiply, "top blend");
    const auto& bg = loaded.layers[0].pixels;
    errors += expect(!bg.empty() && bg[0] == 0 && bg[1] == 0 && bg[2] == 255 && bg[3] == 255,
                     "bottom pixel");
    const auto& ov = loaded.layers[1].pixels;
    const int idx = (1 * 8 + 1) * 4;
    errors += expect(static_cast<int>(ov.size()) > idx + 3 && ov[idx] == 255 && ov[idx + 1] == 0 &&
                         ov[idx + 2] == 0,
                     "top pixel");
  }


  {
    archive* a = archive_read_new();
    errors += expect(a != nullptr, "archive reader");
    if (a != nullptr) {
      archive_read_support_format_zip(a);
      errors += expect(archive_read_open_filename(a, path.c_str(), 16384) == ARCHIVE_OK, "open zip");
      bool found_thumb = false;
      archive_entry* entry = nullptr;
      while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* name = archive_entry_pathname(entry);
        if (name != nullptr && std::string(name) == "Thumbnails/thumbnail.png") {
          found_thumb = true;
        }
        archive_read_data_skip(a);
      }
      errors += expect(found_thumb, "Thumbnails/thumbnail.png present");
      archive_read_free(a);
    }
  }

  unlink(path.c_str());

  {
    const std::string nested_path = temp_ora_path();
    std::vector<std::uint8_t> top_px(2 * 2 * 4, 0);
    top_px[0] = 10;
    top_px[3] = 255;
    std::vector<std::uint8_t> bottom_px(2 * 2 * 4, 255);
    std::string png_error;
    std::vector<std::uint8_t> top_png;
    std::vector<std::uint8_t> bottom_png;
    errors += expect(lundukepaint::encode_png_memory(top_px.data(), 2, 2, 8, top_png, png_error),
                     "encode top png");
    errors += expect(
        lundukepaint::encode_png_memory(bottom_px.data(), 2, 2, 8, bottom_png, png_error),
        "encode bottom png");
    const char* stack_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<image w=\"4\" h=\"3\" version=\"0.0.3\">\n"
        "  <stack>\n"
        "    <stack name=\"Group\" opacity=\"0.5\" x=\"1\" y=\"2\">\n"
        "      <layer name=\"Top\" src=\"data/top.png\" x=\"3\" y=\"4\"/>\n"
        "      <stack name=\"Inner\">\n"
        "        <layer name=\"Bottom\" src=\"data/bottom.png\" x=\"0\" y=\"0\"/>\n"
        "      </stack>\n"
        "    </stack>\n"
        "  </stack>\n"
        "</image>\n";
    const char* mime = "image/openraster";
    archive* writer = archive_write_new();
    errors += expect(writer != nullptr, "nested writer");
    if (writer != nullptr) {
      archive_write_set_format_zip(writer);
      errors += expect(archive_write_open_filename(writer, nested_path.c_str()) == ARCHIVE_OK,
                       "create nested ora");
      archive_write_zip_set_compression_store(writer);
      errors += expect(write_zip_entry(writer, "mimetype", mime, std::strlen(mime)), "mimetype");
      archive_write_zip_set_compression_deflate(writer);
      errors += expect(write_zip_entry(writer, "stack.xml", stack_xml, std::strlen(stack_xml)),
                       "stack.xml");
      errors += expect(write_zip_entry(writer, "data/top.png", top_png.data(), top_png.size()),
                       "top png");
      errors += expect(
          write_zip_entry(writer, "data/bottom.png", bottom_png.data(), bottom_png.size()),
          "bottom png");
      archive_write_close(writer);
      archive_write_free(writer);
    }
    LoadedOra nested = load_ora(nested_path);
    errors += expect(nested.ok(), "nested load");
    if (!nested.ok()) {
      std::fprintf(stderr, "test_ora: nested load error: %s\n", nested.error.c_str());
    }
    errors += expect(nested.nested_groups, "nested groups flag");
    errors += expect(static_cast<int>(nested.layers.size()) == 2, "nested layer count");
    if (nested.layers.size() == 2) {
      errors += expect(nested.layers[0].name == "Bottom", "bottom layer kept");
      errors += expect(nested.layers[1].name == "Top", "top layer kept");
      errors += expect(nested.layers[0].offset_x == 1 && nested.layers[0].offset_y == 2,
                       "inner layer inherits group offset");
      errors += expect(nested.layers[1].offset_x == 4 && nested.layers[1].offset_y == 6,
                       "outer layer adds its own offset");
    }
    errors += expect(count_named(nested.stack, "Group") == 1, "group name kept");
    errors += expect(count_named(nested.stack, "Inner") == 1, "inner stack kept");

    std::vector<std::unique_ptr<Layer>> layers;
    for (const auto& snap : nested.layers) {
      layers.push_back(lundukepaint::layer_from_snapshot(snap));
    }
    auto round = Document::create(nested.width, nested.height, Color::transparent(), "Bottom");
    round->replace_stack(nested.width, nested.height, std::move(layers), 1);
    round->set_ora_stack(nested.stack);
    const std::string again = temp_ora_path();
    std::string save_error;
    errors += expect(save_ora(again, *round, save_error), "save nested ora");
    LoadedOra reloaded = load_ora(again);
    errors += expect(reloaded.ok() && reloaded.nested_groups, "reload keeps groups");
    errors += expect(count_named(reloaded.stack, "Group") == 1, "saved group name");
    errors += expect(count_named(reloaded.stack, "Inner") == 1, "saved inner stack");
    if (reloaded.layers.size() == 2) {
      errors += expect(reloaded.layers[0].name == "Bottom" && reloaded.layers[1].name == "Top",
                       "saved layer order");
      errors += expect(reloaded.layers[0].offset_x == 1 && reloaded.layers[1].offset_x == 4,
                       "saved offsets");
    }
    unlink(nested_path.c_str());
    unlink(again.c_str());
  }

  {
    auto floating = Document::create(4, 4, Color::white(), "Background");
    std::vector<std::uint8_t> stamp(4, 0);
    stamp[0] = 9;
    stamp[1] = 8;
    stamp[2] = 7;
    stamp[3] = 255;
    floating->paste_floating(1, 1, 1, 1, stamp);
    errors += expect(floating->layers().active_layer().pixel(1, 1) == Color::white(),
                     "float is not in the layer yet");
    const std::string float_path = temp_ora_path();
    std::string float_error;
    errors += expect(save_ora(float_path, *floating, float_error), "save before commit");
    LoadedOra uncommitted = load_ora(float_path);
    errors += expect(uncommitted.ok(), "load uncommitted");
    if (!uncommitted.layers.empty()) {
      const auto& px = uncommitted.layers[0].pixels;
      const int idx = (1 * 4 + 1) * 4;
      errors += expect(static_cast<int>(px.size()) > idx + 3 && px[idx] == 255 && px[idx + 1] == 255 &&
                           px[idx + 2] == 255,
                       "save without commit keeps the original");
    }
    errors += expect(floating->commit_floating(), "commit floating");
    errors += expect(floating->layers().active_layer().pixel(1, 1) == (Color{9, 8, 7, 255}),
                     "commit writes the float");
    errors += expect(save_ora(float_path, *floating, float_error), "save after commit");
    LoadedOra committed = load_ora(float_path);
    if (!committed.layers.empty()) {
      const auto& px = committed.layers[0].pixels;
      const int idx = (1 * 4 + 1) * 4;
      errors += expect(static_cast<int>(px.size()) > idx + 3 && px[idx] == 9 && px[idx + 1] == 8 &&
                           px[idx + 2] == 7,
                       "save after commit writes the float");
    }
    unlink(float_path.c_str());
  }

  {
    auto doc = Document::create(1, 1, Color::transparent(), "Only");
    doc->layers().active_layer().set_pixel(0, 0, Color{255, 0, 0, 255});
    lundukepaint::OraNode root;
    root.is_stack = true;
    root.opacity = 0.5f;
    lundukepaint::OraNode leaf;
    leaf.layer_index = 0;
    root.children.push_back(leaf);
    doc->set_ora_stack(std::move(root));
    std::vector<std::uint8_t> px(4, 0);
    doc->layers().composite_rect(px.data(), 4, lundukepaint::Rect{0, 0, 1, 1});
    errors += expect(px[0] == 255 && px[3] > 100 && px[3] < 160, "group opacity composites");
  }

  {
    errors += expect(lundukepaint::replace_path_extension("/home/me.backup/portrait", ".png") ==
                         "/home/me.backup/portrait.png",
                     "dot in a parent directory is not the extension");
    errors += expect(lundukepaint::replace_path_extension("me.backup/portrait", "png") ==
                         "me.backup/portrait.png",
                     "relative me.backup/portrait becomes me.backup/portrait.png");
  }

  {
    char dir[] = "/tmp/lunduke-paint-atomic-XXXXXX";
    if (mkdtemp(dir) == nullptr) {
      std::fprintf(stderr, "test_ora: atomic mkdtemp failed\n");
      return 1;
    }
    const std::string folder(dir);
    const std::string dest = folder + "/portrait.png";
    std::vector<std::uint8_t> rgba(4, 255);
    rgba[0] = 10;
    rgba[1] = 20;
    rgba[2] = 30;
    std::string save_error;
    errors += expect(lundukepaint::save_flat_image(dest, lundukepaint::ImageFormat::Png, rgba.data(),
                                                   1, 1, 4, 90, save_error),
                     "seed flat image");
    const std::vector<std::uint8_t> previous = read_file(dest);
    errors += expect(!previous.empty(), "seed file has bytes");
    chmod(dir, 0555);
    rgba[0] = 99;
    errors += expect(!lundukepaint::save_flat_image(dest, lundukepaint::ImageFormat::Png, rgba.data(),
                                                    1, 1, 4, 90, save_error),
                     "flat save fails when the directory is not writable");
    errors += expect(read_file(dest) == previous, "failed flat save leaves the previous bytes");
    chmod(dir, 0755);

    const std::string ora_dest = folder + "/picture.ora";
    auto ora_doc = Document::create(2, 2, Color::white(), "Background");
    errors += expect(save_ora(ora_dest, *ora_doc, save_error), "seed ora");
    const std::vector<std::uint8_t> ora_previous = read_file(ora_dest);
    chmod(dir, 0555);
    errors += expect(!save_ora(ora_dest, *ora_doc, save_error),
                     "ora save fails when the directory is not writable");
    errors += expect(read_file(ora_dest) == ora_previous, "failed ora save leaves the previous bytes");
    chmod(dir, 0755);

    const std::string outside = folder + "/outside.png";
    errors += expect(lundukepaint::save_flat_image(outside, lundukepaint::ImageFormat::Png,
                                                   rgba.data(), 1, 1, 4, 90, save_error),
                     "seed symlink target");
    const std::vector<std::uint8_t> outside_bytes = read_file(outside);
    const std::string link = folder + "/linked.png";
    errors += expect(symlink(outside.c_str(), link.c_str()) == 0, "symlink destination");
    rgba[0] = 1;
    errors += expect(!lundukepaint::save_flat_image(link, lundukepaint::ImageFormat::Png, rgba.data(),
                                                    1, 1, 4, 90, save_error),
                     "save refuses a symlink");
    errors += expect(read_file(outside) == outside_bytes, "symlink target is unchanged");
    unlink(link.c_str());
    unlink(dest.c_str());
    unlink(ora_dest.c_str());
    unlink(outside.c_str());
    rmdir(dir);
  }

  {
    std::vector<std::uint8_t> data(10, 1);
    std::string cap_error;
    const std::uint8_t extra[8] = {2, 2, 2, 2, 2, 2, 2, 2};
    errors += expect(!lundukepaint::ora_append_capped(data, extra, 8, 12, cap_error),
                     "append stops at the cap");
    errors += expect(data.size() == 10, "rejected append does not grow");
    errors += expect(lundukepaint::ora_append_capped(data, extra, 2, 12, cap_error),
                     "append under the cap");
    errors += expect(data.size() == 12, "capped append grew by the accepted bytes");

    const std::string capped_path = temp_ora_path();
    auto capped_doc = Document::create(2, 2, Color{1, 2, 3, 255}, "Background");
    std::string capped_error;
    errors += expect(save_ora(capped_path, *capped_doc, capped_error), "ora for the cap test");
    std::map<std::string, std::vector<std::uint8_t>> files;
    errors += expect(!lundukepaint::read_ora_zip(capped_path, files, capped_error, 32,
                                                 lundukepaint::kMaxOraTotalBytes),
                     "known entry above the cap is rejected");
    std::vector<std::uint8_t> preview_png;
    errors += expect(!lundukepaint::load_ora_preview_png_limited(capped_path, preview_png, 32),
                     "preview refuses an entry above the cap");

    const std::vector<std::uint8_t> payload(64, 0xab);
    const std::vector<std::uint8_t> unknown = unknown_size_zip("blob", payload);
    const std::string unknown_path = temp_ora_path();
    {
      std::ofstream out(unknown_path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(unknown.data()),
                static_cast<std::streamsize>(unknown.size()));
    }
    files.clear();
    errors += expect(!lundukepaint::read_ora_zip(unknown_path, files, capped_error, 16,
                                                 lundukepaint::kMaxOraTotalBytes),
                     "unknown-size entry stops at the cap");
    unlink(capped_path.c_str());
    unlink(unknown_path.c_str());
  }

  {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<image w=\"1\" h=\"1\">\n<stack>\n";
    for (int i = 0; i < lundukepaint::kHardMaxLayers + 1; ++i) {
      xml += "<layer name=\"L\" src=\"data/missing.png\"/>\n";
    }
    xml += "</stack>\n</image>\n";
    const char* mime = "image/openraster";
    const std::string many_path = temp_ora_path();
    archive* writer = archive_write_new();
    if (writer != nullptr) {
      archive_write_set_format_zip(writer);
      archive_write_open_filename(writer, many_path.c_str());
      write_zip_entry(writer, "mimetype", mime, std::strlen(mime));
      write_zip_entry(writer, "stack.xml", xml.data(), xml.size());
      archive_write_close(writer);
      archive_write_free(writer);
    }
    LoadedOra many = load_ora(many_path);
    errors += expect(!many.ok(), "too many layers fails closed");
    errors += expect(many.error.find("too many layers") != std::string::npos, "layer cap error");
    unlink(many_path.c_str());
  }

  {
    std::vector<std::uint8_t> px(4, 255);
    px[0] = 255;
    px[1] = 0;
    px[2] = 0;
    std::vector<std::uint8_t> png;
    std::string png_error;
    errors += expect(lundukepaint::encode_png_memory(px.data(), 1, 1, 4, png, png_error),
                     "root opacity png");
    const char* stack_xml =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<image w=\"1\" h=\"1\" version=\"0.0.3\">\n"
        "  <stack opacity=\"0.5\">\n"
        "    <layer name=\"Only\" src=\"data/layer.png\"/>\n"
        "  </stack>\n"
        "</image>\n";
    const char* mime = "image/openraster";
    const std::string root_path = temp_ora_path();
    archive* writer = archive_write_new();
    if (writer != nullptr) {
      archive_write_set_format_zip(writer);
      archive_write_open_filename(writer, root_path.c_str());
      write_zip_entry(writer, "mimetype", mime, std::strlen(mime));
      write_zip_entry(writer, "stack.xml", stack_xml, std::strlen(stack_xml));
      write_zip_entry(writer, "data/layer.png", png.data(), png.size());
      archive_write_close(writer);
      archive_write_free(writer);
    }
    LoadedOra rooted = load_ora(root_path);
    errors += expect(rooted.ok(), "root opacity loads");
    errors += expect(rooted.has_stack, "root stack opacity installs the group");
    errors += expect(!rooted.nested_groups, "a single stack is not nested");
    unlink(root_path.c_str());
  }

  {
    auto doc = Document::create(1, 1, Color::transparent(), "Red");
    doc->layers().active_layer().set_pixel(0, 0, Color{255, 0, 0, 255});
    doc->add_layer();
    Layer& top = doc->layers().active_layer();
    top.set_name("Blue");
    top.set_pixel(0, 0, Color{0, 0, 255, 255});
    top.set_blend(BlendMode::Multiply);
    auto install = [&](bool isolate) {
      lundukepaint::OraNode root;
      root.is_stack = true;
      lundukepaint::OraNode group;
      group.is_stack = true;
      group.opacity = 0.5f;
      group.isolate = isolate;
      lundukepaint::OraNode child;
      child.layer_index = 1;
      group.children.push_back(child);
      lundukepaint::OraNode backdrop;
      backdrop.layer_index = 0;
      root.children.push_back(group);
      root.children.push_back(backdrop);
      doc->set_ora_stack(std::move(root));
    };
    install(false);
    std::uint8_t non_isolated[4] = {};
    doc->layers().composite_rect(non_isolated, 4, lundukepaint::Rect{0, 0, 1, 1});
    install(true);
    std::uint8_t isolated[4] = {};
    doc->layers().composite_rect(isolated, 4, lundukepaint::Rect{0, 0, 1, 1});
    errors += expect(std::memcmp(non_isolated, isolated, 4) != 0,
                     "non-isolated group differs from its isolated twin");
  }

  {
    lundukepaint::ora_debug_set_png_cache_cap(1);
    errors += expect(lundukepaint::ora_png_cache_entries() == 0, "tiny cap drops cached pngs");
    lundukepaint::ora_debug_set_png_cache_cap(32u * 1024u * 1024u);
    const std::size_t before = lundukepaint::ora_png_cache_entries();
    {
      auto cached = Document::create(4, 4, Color{9, 8, 7, 255}, "Cached");
      std::string cache_error;
      const std::string cache_path = temp_ora_path();
      errors += expect(save_ora(cache_path, *cached, cache_error), "save to fill the png cache");
      errors += expect(lundukepaint::ora_png_cache_entries() > before, "save caches a layer png");
      unlink(cache_path.c_str());
    }
    errors += expect(lundukepaint::ora_png_cache_entries() == before,
                     "destroying the layer drops its png cache entry");

    lundukepaint::ora_debug_set_png_cache_cap(1);
    lundukepaint::ora_debug_set_png_cache_cap(32u * 1024u * 1024u);
    auto small = Document::create(1, 1, Color{1, 2, 3, 255}, "Small");
    auto large = Document::create(48, 48, Color{4, 5, 6, 255}, "Large");
    large->layers().active_layer().set_pixel(10, 10, Color{255, 0, 0, 255});
    std::string lru_error;
    const std::string small_path = temp_ora_path();
    const std::string large_path = temp_ora_path();
    errors += expect(save_ora(small_path, *small, lru_error), "save small for lru");
    const std::size_t after_small = lundukepaint::ora_png_cache_bytes();
    errors += expect(save_ora(large_path, *large, lru_error), "save large for lru");
    const std::size_t after_both = lundukepaint::ora_png_cache_bytes();
    errors += expect(after_both > after_small, "second layer adds cache bytes");
    const std::size_t large_bytes = after_both - after_small;
    lundukepaint::ora_debug_set_png_cache_cap(large_bytes);
    errors += expect(lundukepaint::ora_png_cache_bytes() <= large_bytes, "byte cap evicts lru entries");
    errors += expect(lundukepaint::ora_png_cache_entries() == 1, "lru keeps the newest layer png");
    unlink(small_path.c_str());
    unlink(large_path.c_str());
    lundukepaint::ora_debug_set_png_cache_cap(32u * 1024u * 1024u);
  }

  {
    static const unsigned char kGif[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x80, 0x00, 0x00,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00, 0x3b};
    static const unsigned char kAnim[] = {
        0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x91, 0x00, 0x00, 0xff, 0xff,
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x21, 0xf9, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x02, 0x02, 0x44, 0x01, 0x00,
        0x21, 0xf9, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
        0x01, 0x00, 0x00, 0x02, 0x02, 0x4c, 0x01, 0x00, 0x3b};
    const std::string still = temp_ora_path() + ".gif";
    const std::string anim = temp_ora_path() + ".gif";
    {
      std::ofstream out(still, std::ios::binary);
      out.write(reinterpret_cast<const char*>(kGif), sizeof(kGif));
    }
    {
      std::ofstream out(anim, std::ios::binary);
      out.write(reinterpret_cast<const char*>(kAnim), sizeof(kAnim));
    }
    errors += expect(!lundukepaint::image_has_multiple_frames(still), "a one-frame gif is static");
    errors += expect(lundukepaint::image_has_multiple_frames(anim), "a multi-frame gif is reported");
    lundukepaint::LoadedImage loaded = lundukepaint::load_flat_image(anim);
    errors += expect(loaded.ok() && loaded.animated, "load keeps the first frame and flags the animation");
    unlink(still.c_str());
    unlink(anim.c_str());
  }

  if (errors != 0) {
    std::fprintf(stderr, "test_ora: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_ora: ok\n");
  return 0;
}
