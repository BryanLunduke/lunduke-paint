// SPDX-License-Identifier: GPL-3.0-or-later
// 0.9.5-2 pre-1.0 fixes, no display needed:
//   * Save / the close prompt send GIF and other read-only formats to Save As
//     (save_needs_chooser), which proposes the same title as PNG.
//   * The large-image size comes from the header without a full decode.

#include "io/image_io.hpp"
#include "io/save_target.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <glib.h>

using namespace lundukepaint;

namespace {
int errors = 0;
void expect(bool cond, const char* msg) {
  std::printf("%s: %s\n", cond ? "PASS" : "FAIL", msg);
  if (!cond) ++errors;
}
}  // namespace

int main() {
  // Fix 3: which paths Save may write in place.
  expect(save_needs_chooser(""), "no path needs Save As");
  expect(save_needs_chooser("/tmp/anim.gif"), "GIF needs Save As");
  expect(save_needs_chooser("/tmp/ANIM.GIF"), "GIF (upper case) needs Save As");
  expect(save_needs_chooser("/tmp/scan.tiff"), "TIFF needs Save As");
  expect(save_needs_chooser("/tmp/photo.webp"), "WebP needs Save As");
  expect(save_needs_chooser("/tmp/noext"), "no extension needs Save As");
  expect(!save_needs_chooser("/tmp/a.png"), "PNG saves in place");
  expect(!save_needs_chooser("/tmp/a.JPG"), "JPEG saves in place");
  expect(!save_needs_chooser("/tmp/a.jpeg"), "jpeg saves in place");
  expect(!save_needs_chooser("/tmp/a.bmp"), "BMP saves in place");
  expect(!save_needs_chooser("/tmp/a.ora"), "OpenRaster saves in place");
  expect(!save_needs_chooser("/tmp/dir.gif/a.png"), "a dot in a folder is not an extension");

  expect(save_as_png_name("/tmp/anim.gif") == "anim.png", "anim.gif proposes anim.png");
  expect(save_as_png_name("/tmp/Scan.TIFF") == "Scan.png", "Scan.TIFF proposes Scan.png");
  expect(save_as_png_name("/tmp/my.drawing") == "my.drawing.png", "a long suffix is kept");
  expect(save_as_png_name("/tmp/noext") == "noext.png", "no extension gets .png");
  expect(save_as_png_name("/tmp/.gif") == ".gif.png" || save_as_png_name("/tmp/.gif") == "untitled.png",
         "a bare .gif still gets a usable name");
  expect(save_as_png_name("/tmp/na\xc3\xafve \xe7\x94\xbb.gif") == "na\xc3\xafve \xe7\x94\xbb.png",
         "UTF-8 title kept");

  // Fix 4: header size without a decode.
  char tmpl[] = "/tmp/lp-0952-XXXXXX";
  const char* dir = mkdtemp(tmpl);
  expect(dir != nullptr, "temp dir");
  if (dir == nullptr) return 1;
  const std::string wide = std::string(dir) + "/wide.png";
  std::vector<std::uint8_t> px(static_cast<std::size_t>(8300) * 2 * 4, 200);
  std::string err;
  expect(save_flat_image(wide, ImageFormat::Png, px.data(), 8300, 2, 8300 * 4, 90, err),
         "8300x2 PNG fixture written");
  const int before = flat_image_decode_count();
  int w = 0;
  int h = 0;
  expect(flat_image_header_size(wide, w, h), "header size read");
  expect(w == 8300 && h == 2, "header size is 8300x2");
  expect(flat_image_decode_count() == before, "reading the header decoded nothing");
  expect(!flat_image_header_size(std::string(dir) + "/missing.png", w, h), "missing file: no size");
  const LoadedImage loaded = load_flat_image(wide);
  expect(loaded.ok() && loaded.width == 8300, "full decode still works");
  expect(flat_image_decode_count() == before + 1, "full decode is counted");

  const std::string cmd = std::string("rm -rf '") + dir + "'";
  if (std::system(cmd.c_str()) != 0) std::printf("cleanup failed\n");
  std::printf("%d failure(s)\n", errors);
  return errors == 0 ? 0 : 1;
}
