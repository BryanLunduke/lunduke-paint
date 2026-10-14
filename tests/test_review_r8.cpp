// SPDX-License-Identifier: GPL-3.0-or-later
// Round 8, without a display: synthetic recovery content never poisons the
// layer PNG cache (P-1), decode bombs are refused from the header (P-2),
// layer and whole-image undo steps count against the byte cap (P-3), idle
// recovery ticks are skipped (P-6) and unchanged layers are not copied (P-7).

#include "doc/document.hpp"
#include "doc/history.hpp"
#include "io/crash_recovery.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <glib.h>
#include <unistd.h>

using namespace lundukepaint;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
  std::printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) {
    ++g_failures;
  }
}

long peak_kb() {
  std::ifstream f("/proc/self/status");
  std::string line;
  while (std::getline(f, line)) {
    if (line.rfind("VmHWM", 0) == 0) {
      return std::stol(line.substr(6));
    }
  }
  return 0;
}

void put32(std::vector<std::uint8_t>& v, std::uint32_t x) {
  v.push_back(static_cast<std::uint8_t>(x >> 24));
  v.push_back(static_cast<std::uint8_t>(x >> 16));
  v.push_back(static_cast<std::uint8_t>(x >> 8));
  v.push_back(static_cast<std::uint8_t>(x));
}

// A PNG header claiming 40000x40000 RGBA. The body does not matter: it must
// be refused before any decode.
std::vector<std::uint8_t> bomb_png() {
  std::vector<std::uint8_t> v = {137, 80, 78, 71, 13, 10, 26, 10};
  put32(v, 13);
  for (char c : std::string("IHDR")) v.push_back(static_cast<std::uint8_t>(c));
  put32(v, 40000);
  put32(v, 40000);
  v.insert(v.end(), {8, 6, 0, 0, 0});
  put32(v, 0);
  return v;
}

std::string tmp_path(const char* name) {
  return std::string(g_get_tmp_dir()) + "/lp-r8-" + std::to_string(getpid()) + "-" + name;
}

void test_p1_overlay_does_not_poison_cache() {
  auto dp = Document::create(64, 64, Color::white(), "Background");
  Document& doc = *dp;
  OraSnapshot s = capture_ora_snapshot(doc, true);
  // What start_recovery_save() now does for a tool overlay / float / preview.
  for (std::size_t i = 0; i < s.layers[0].pixels.size(); i += 4) {
    s.layers[0].pixels[i + 1] = 0;
    s.layers[0].pixels[i + 2] = 0;
  }
  s.layers[0].identity = 0;
  std::string e;
  const std::string rec = tmp_path("rec.ora");
  const std::string real = tmp_path("real.ora");
  save_ora_snapshot(rec, s, e);
  doc.layers().at(0).set_pixel(0, 0, Color::black());
  check(save_ora(real, doc, e), "P-1 save_ora after overlay snapshot");
  LoadedOra o = load_ora(real);
  bool ok = o.ok() && !o.layers.empty();
  if (ok) {
    const auto& p = o.layers[0].pixels;
    const std::size_t k = (10 * 64 + 10) * 4;
    ok = p[k] == 255 && p[k + 1] == 255 && p[k + 2] == 255 && p[0] == 0 && p[1] == 0 && p[2] == 0;
  }
  check(ok, "P-1 saved .ora has the real edit and no overlay");
  unlink(rec.c_str());
  unlink(real.c_str());
}

void test_p2_bomb_refused_from_header() {
  const std::vector<std::uint8_t> png = bomb_png();
  const long before = peak_kb();
  LoadedImage mem;
  check(!decode_png_memory(png.data(), png.size(), mem) &&
            mem.error.find("16384") != std::string::npos,
        "P-2 in-memory (.ora layer) bomb refused with the size message");
  const std::string path = tmp_path("bomb.png");
  {
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
  }
  LoadedImage file = load_flat_image(path);
  check(!file.ok() && file.error.find("16384") != std::string::npos,
        "P-2 file bomb refused with the size message");
  check(!image_has_multiple_frames(path), "P-2 frame probe does not decode the bomb");
  check(peak_kb() - before < 64 * 1024, "P-2 refusing the bomb allocates < 64 MB");
  check(png_header_size_ok(png.data(), 10), "P-2 short buffer left to the decoder");
  unlink(path.c_str());
}

void test_p3_layer_steps_count() {
  auto d = Document::create(1000, 1000, Color::white(), "Background");
  const std::size_t layer_bytes = 1000u * 1000u * 4u;
  d->add_layer();
  check(d->history().memory_bytes() < layer_bytes / 4, "P-3 Add layer stores no zero buffer");
  d->delete_layer();
  check(d->history().memory_bytes() >= layer_bytes, "P-3 Delete layer counts its snapshot");
  d->history().set_byte_cap(10 * layer_bytes);
  for (int i = 0; i < 30; ++i) {
    d->add_layer();
    d->delete_layer();
  }
  check(d->history().memory_bytes() <= 11 * layer_bytes,
        "P-3 30 add/delete cycles stay within the undo byte cap");
  check(d->history().base_dropped(), "P-3 cap dropped old layer steps");
  auto f = Document::create(500, 400, Color::white(), "Background");
  f->add_layer();
  f->add_layer();
  const std::size_t before = f->history().memory_bytes();
  f->flatten();
  check(f->history().memory_bytes() - before >= 3u * 500u * 400u * 4u,
        "P-3 Flatten counts every layer snapshot");
}

void test_p6_signature() {
  auto d = Document::create(32, 32, Color::white(), "Background");
  const std::string a = crash_recovery::recovery_signature(*d);
  check(a == crash_recovery::recovery_signature(*d), "P-6 unchanged document keeps its fingerprint");
  d->layers().at(0).set_pixel(1, 1, Color::black());
  const std::string b = crash_recovery::recovery_signature(*d);
  check(a != b, "P-6 pixel edit changes the fingerprint");
  d->layers().at(0).set_name("Renamed");
  check(b != crash_recovery::recovery_signature(*d), "P-6 layer rename changes the fingerprint");
}

void test_p7_no_copy_for_cached_layers() {
  auto d = Document::create(128, 96, Color::white(), "Background");
  d->layers().at(0).set_pixel(3, 4, Color::black());
  const std::string a = tmp_path("p7a.ora");
  const std::string b = tmp_path("p7b.ora");
  std::string e;
  check(save_ora(a, *d, e), "P-7 first save fills the PNG cache");
  OraSnapshot s = capture_ora_snapshot(*d, false);
  check(s.layers.size() == 1 && s.layers[0].pixels.empty() && !s.layers[0].cached_png.empty(),
        "P-7 recovery snapshot reuses the cached PNG without copying pixels");
  check(save_ora_snapshot(b, s, e), "P-7 snapshot without pixels saves");
  LoadedOra o = load_ora(b);
  check(o.ok() && o.layers.size() == 1 && o.layers[0].pixels[(4 * 128 + 3) * 4] == 0 &&
            o.layers[0].pixels[0] == 255,
        "P-7 reloaded recovery file has the real pixels");
  ensure_snapshot_pixels(s.layers[0], d->layers().at(0));
  check(!s.layers[0].pixels.empty() && s.layers[0].cached_png.empty(),
        "P-7 overlay path materialises pixels and drops the cached PNG");
  unlink(a.c_str());
  unlink(b.c_str());
}

}  // namespace

int main() {
  test_p1_overlay_does_not_poison_cache();
  test_p2_bomb_refused_from_header();
  test_p3_layer_steps_count();
  test_p6_signature();
  test_p7_no_copy_for_cached_layers();
  std::printf("%d failure(s)\n", g_failures);
  return g_failures == 0 ? 0 : 1;
}
