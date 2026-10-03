// SPDX-License-Identifier: GPL-3.0-or-later

#include "doc/document.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"
#include "raster/blend.hpp"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <archive.h>
#include <archive_entry.h>
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

  if (errors != 0) {
    std::fprintf(stderr, "test_ora: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_ora: ok\n");
  return 0;
}
