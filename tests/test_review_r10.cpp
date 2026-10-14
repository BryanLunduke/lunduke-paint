// SPDX-License-Identifier: GPL-3.0-or-later
// Round 10, without a display (P-1): a clipboard image's size is read from
// its encoded header before any pixels are decoded, so a 20000x20000 PNG is
// measured without allocating its 1.6 GB buffer; small images still decode.

#include "io/image_io.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include <zlib.h>

using namespace lundukepaint;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}

void be32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v >> 24));
  out.push_back(static_cast<std::uint8_t>(v >> 16));
  out.push_back(static_cast<std::uint8_t>(v >> 8));
  out.push_back(static_cast<std::uint8_t>(v));
}

void chunk(std::vector<std::uint8_t>& out, const char* type, const std::vector<std::uint8_t>& data) {
  be32(out, static_cast<std::uint32_t>(data.size()));
  const std::size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  be32(out, static_cast<std::uint32_t>(crc32(0, out.data() + start, static_cast<uInt>(out.size() - start))));
}

// A PNG claiming width x height RGBA, with the start of a zero-filled IDAT.
std::vector<std::uint8_t> huge_png(std::uint32_t width, std::uint32_t height) {
  std::vector<std::uint8_t> png = {137, 80, 78, 71, 13, 10, 26, 10};
  std::vector<std::uint8_t> ihdr;
  be32(ihdr, width);
  be32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk(png, "IHDR", ihdr);
  std::vector<std::uint8_t> raw(static_cast<std::size_t>(width) * 4 + 1, 0);
  std::vector<std::uint8_t> idat(compressBound(static_cast<uLong>(raw.size())));
  uLongf n = static_cast<uLongf>(idat.size());
  compress(idat.data(), &n, raw.data(), static_cast<uLong>(raw.size()));
  idat.resize(n);
  chunk(png, "IDAT", idat);
  return png;
}

long peak_rss_kb() {
  std::ifstream in("/proc/self/status");
  std::string line;
  while (std::getline(in, line)) {
    if (line.rfind("VmHWM:", 0) == 0) {
      return std::atol(line.c_str() + 6);
    }
  }
  return -1;
}

}  // namespace

int main() {
  const long before = peak_rss_kb();
  const auto png = huge_png(20000, 20000);
  int w = 0;
  int h = 0;
  const bool sized = encoded_image_size(png.data(), png.size(), "image/png", w, h);
  check(sized && w == 20000 && h == 20000, "20000x20000 clipboard PNG is measured from its header");
  const long after = peak_rss_kb();
  std::printf("VmHWM %ld -> %ld kB\n", before, after);
  check(after - before < 64 * 1024, "measuring it does not allocate the pixel buffer");

  int sw = 0;
  int sh = 0;
  check(encoded_image_size(png.data(), png.size(), "", sw, sh) && sw == 20000,
        "the size is also read when the type is sniffed");

  const std::uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  check(!encoded_image_size(junk, sizeof junk, "image/png", sw, sh), "junk is not an image");

  std::vector<std::uint8_t> rgba(3 * 2 * 4, 0);
  for (std::size_t i = 0; i < rgba.size(); ++i) {
    rgba[i] = static_cast<std::uint8_t>(i * 9);
  }
  std::vector<std::uint8_t> small;
  std::string err;
  check(encode_png_memory(rgba.data(), 3, 2, 3 * 4, small, err), "small PNG encodes");
  check(encoded_image_size(small.data(), small.size(), "image/png", sw, sh) && sw == 3 && sh == 2,
        "small PNG is measured");
  LoadedImage img;
  bool oom = true;
  check(decode_encoded_image(small.data(), small.size(), "image/png", img, oom) && !oom &&
            img.width == 3 && img.height == 2 && img.rgba == rgba,
        "small PNG decodes exactly");

  check(!decode_encoded_image(junk, sizeof junk, "image/png", img, oom) && !oom,
        "junk fails to decode without claiming out of memory");

  return g_failures == 0 ? 0 : 1;
}
