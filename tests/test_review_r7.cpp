// SPDX-License-Identifier: GPL-3.0-or-later
// Round 7, without a display: file modes, the .ora companion name, one
// flatten warning per target, JPEG-quality visibility, and an honest undo
// budget (including a 4000 px picture).

#include "doc/commands_pixels.hpp"
#include "doc/document.hpp"
#include "doc/history.hpp"
#include "io/crash_recovery.hpp"
#include "io/image_io.hpp"
#include "io/ora.hpp"
#include "io/save_target.hpp"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <glib.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

using lundukepaint::Color;
using lundukepaint::Document;
using lundukepaint::ImageFormat;
using lundukepaint::Layer;
using lundukepaint::PixelPatchCommand;
using lundukepaint::Rect;
using lundukepaint::companion_ora_path;
using lundukepaint::flat_save_ack_key;
using lundukepaint::flat_save_message;
using lundukepaint::kDefaultUndoBytes;
using lundukepaint::kUndoDroppedNotice;
using lundukepaint::ora_companion_needs_confirm;
using lundukepaint::save_flat_image;
using lundukepaint::save_ora;
using lundukepaint::save_shows_jpeg_quality;
using lundukepaint::should_warn_flat_save;
using lundukepaint::suggested_undo_bytes;

int errors = 0;

void expect(bool cond, const char* msg) {
  if (!cond) {
    std::fprintf(stderr, "test_review_r7: %s\n", msg);
    ++errors;
  }
}

std::string make_temp_dir(const char* prefix) {
  char tmpl[64];
  std::snprintf(tmpl, sizeof(tmpl), "/tmp/%sXXXXXX", prefix);
  char* dir = mkdtemp(tmpl);
  return dir == nullptr ? std::string() : std::string(dir);
}

mode_t mode_of(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return 0;
  }
  return st.st_mode & 0777;
}

bool same_owner(const std::string& path, uid_t uid, gid_t gid) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }
  return st.st_uid == uid && st.st_gid == gid;
}

class UmaskGuard {
public:
  explicit UmaskGuard(mode_t next) : saved_(umask(next)) {}
  ~UmaskGuard() { umask(saved_); }

private:
  mode_t saved_;
};

void paint_checker(Layer& layer, int phase) {
  const int w = layer.width();
  const int h = layer.height();
  const int stride = layer.stride();
  std::uint8_t* base = layer.pixels();
  for (int y = 0; y < h; ++y) {
    std::uint8_t* row = base + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < w; ++x) {
      const std::uint8_t v =
          static_cast<std::uint8_t>((((x + phase) >> 2) ^ (y >> 2)) & 1 ? 210 : 30);
      std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      p[0] = v;
      p[1] = static_cast<std::uint8_t>(40 + phase * 20);
      p[2] = static_cast<std::uint8_t>(255 - v);
      p[3] = 255;
    }
  }
}

void commit_checker(Document& doc, int phase, const char* name) {
  Layer& layer = doc.layers().active_layer();
  Layer before(layer.width(), layer.height(), Color::transparent(), "before");
  before.copy_from(layer);
  paint_checker(layer, phase);
  auto cmd = PixelPatchCommand::from_layers(before, layer, Rect{0, 0, layer.width(), layer.height()},
                                            name, doc.layers().active_index());
  expect(cmd && !cmd->empty(), name);
  doc.history().commit_applied(std::move(cmd));
}

std::vector<std::uint8_t> white_rgba() {
  return {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
}

void test_modes() {
  UmaskGuard guard(022);
  const std::string dir = make_temp_dir("lunduke-r7-mode-");
  expect(!dir.empty(), "mode temp dir");
  if (dir.empty()) {
    return;
  }
  auto doc = Document::create(2, 2, Color::white(), "Background");
  const std::vector<std::uint8_t> rgba = white_rgba();
  std::string error;

  const char* flats[] = {"pic.png", "pic.jpg", "pic.bmp"};
  const ImageFormat formats[] = {ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Bmp};
  for (int i = 0; i < 3; ++i) {
    const std::string path = dir + "/" + flats[i];
    expect(save_flat_image(path, formats[i], rgba.data(), 2, 2, 8, 90, error), flats[i]);
    if (mode_of(path) != 0644) {
      std::fprintf(stderr, "test_review_r7: %s mode %o, expected 0644\n", flats[i], mode_of(path));
      ++errors;
    }
  }
  const std::string ora = dir + "/pic.ora";
  expect(save_ora(ora, *doc, error), "new ora");
  expect(mode_of(ora) == 0644, "new ora is 0644 under umask 022");

  const mode_t keep[] = {0664, 0644};
  for (mode_t want : keep) {
    const std::string path = dir + (want == 0664 ? "/keep.png" : "/keep644.png");
    expect(save_flat_image(path, ImageFormat::Png, rgba.data(), 2, 2, 8, 90, error), "seed png");
    expect(chmod(path.c_str(), want) == 0, "chmod seed");
    struct stat before;
    expect(stat(path.c_str(), &before) == 0, "stat seed");
    expect(save_flat_image(path, ImageFormat::Png, rgba.data(), 2, 2, 8, 90, error), "resave png");
    if (mode_of(path) != want) {
      std::fprintf(stderr, "test_review_r7: resave png mode %o, expected %o\n", mode_of(path), want);
      ++errors;
    }
    expect(same_owner(path, before.st_uid, before.st_gid), "png resave keeps owner");

    const std::string ora_path = dir + (want == 0664 ? "/keep.ora" : "/keep644.ora");
    expect(save_ora(ora_path, *doc, error), "seed ora");
    expect(chmod(ora_path.c_str(), want) == 0, "chmod ora");
    struct stat ora_before;
    expect(stat(ora_path.c_str(), &ora_before) == 0, "stat ora");
    expect(save_ora(ora_path, *doc, error), "resave ora");
    if (mode_of(ora_path) != want) {
      std::fprintf(stderr, "test_review_r7: resave ora mode %o, expected %o\n", mode_of(ora_path),
                   want);
      ++errors;
    }
    expect(same_owner(ora_path, ora_before.st_uid, ora_before.st_gid), "ora resave keeps owner");
  }

  // JPEG and BMP resave too, not only PNG.
  for (int i = 1; i < 3; ++i) {
    const std::string path = dir + "/" + flats[i];
    expect(chmod(path.c_str(), 0664) == 0, "chmod flat");
    expect(save_flat_image(path, formats[i], rgba.data(), 2, 2, 8, 90, error), "resave flat");
    expect(mode_of(path) == 0664, "jpeg/bmp resave keeps 0664");
  }

  const std::string state = make_temp_dir("lunduke-r7-xdg-");
  expect(!state.empty(), "state temp dir");
  if (!state.empty()) {
    g_setenv("XDG_STATE_HOME", state.c_str(), TRUE);
    auto recovery = Document::create(2, 2, Color::white(), "Background");
    expect(lundukepaint::crash_recovery::write_document(*recovery, error), "autosave create");
    const std::string rec = lundukepaint::crash_recovery::autosave_path();
    expect(mode_of(rec) == 0644, "autosave honors umask 022");
    expect(chmod(rec.c_str(), 0664) == 0, "chmod autosave");
    expect(lundukepaint::crash_recovery::write_document(*recovery, error), "autosave replace");
    expect(mode_of(rec) == 0664, "autosave replace keeps 0664");
    lundukepaint::crash_recovery::clear();
    g_unsetenv("XDG_STATE_HOME");
  }
}

void test_companion_and_warnings() {
  expect(companion_ora_path("/tmp/flat-test.png") == "/tmp/flat-test.ora", "png companion");
  expect(companion_ora_path("/tmp/flat-test.jpeg") == "/tmp/flat-test.ora", "jpeg companion");
  expect(companion_ora_path("/tmp/photo.JPG") == "/tmp/photo.ora", "JPG companion");
  expect(companion_ora_path("/tmp/my.drawing.png") == "/tmp/my.drawing.ora",
         "long title keeps the dot");
  expect(companion_ora_path("/tmp/archive.tar.png") == "/tmp/archive.tar.ora",
         "unknown short suffix is kept");
  expect(companion_ora_path("flat-test.ora") == "flat-test.ora", "already ora");
  expect(!ora_companion_needs_confirm(false, false), "missing file needs no confirm");
  expect(ora_companion_needs_confirm(true, false), "a stranger file is confirmed");
  expect(!ora_companion_needs_confirm(true, true), "our own companion is not confirmed");

  expect(should_warn_flat_save(ImageFormat::Png, true, false, false, false),
         "first multi-layer png warns");
  expect(!should_warn_flat_save(ImageFormat::Png, true, false, true, false),
         "Save As already asked, so save_to_path does not");
  expect(!should_warn_flat_save(ImageFormat::Png, true, false, false, true),
         "an acked target does not warn on Ctrl+S");
  expect(!should_warn_flat_save(ImageFormat::Ora, true, true, false, false), "ora does not flatten-warn");
  expect(!should_warn_flat_save(ImageFormat::Png, false, false, false, false),
         "a single opaque png does not warn");
  expect(should_warn_flat_save(ImageFormat::Jpeg, false, true, false, false),
         "a transparent jpeg warns once");
  expect(!should_warn_flat_save(ImageFormat::Jpeg, false, true, false, true),
         "that jpeg target is not asked again");

  const std::string png_msg = flat_save_message(ImageFormat::Png, true, false);
  expect(png_msg.find("multiple layers") != std::string::npos, "png warning mentions layers");
  expect(png_msg.find("alpha") != std::string::npos, "png warning mentions alpha in the same text");
  const std::string jpeg_msg = flat_save_message(ImageFormat::Jpeg, true, true);
  expect(jpeg_msg.find("onto white") != std::string::npos, "jpeg warning mentions white");
  expect(flat_save_ack_key("/tmp/a.png", ImageFormat::Png, true, false) !=
             flat_save_ack_key("/tmp/b.png", ImageFormat::Png, true, false),
         "different paths are different targets");
  expect(flat_save_ack_key("/tmp/a.png", ImageFormat::Png, true, false) !=
             flat_save_ack_key("/tmp/a.png", ImageFormat::Jpeg, true, false),
         "different formats are different targets");
  expect(flat_save_ack_key("/tmp/a.png", ImageFormat::Png, true, false) !=
             flat_save_ack_key("/tmp/a.png", ImageFormat::Png, false, false),
         "gaining a layer is a new target");

  auto doc = Document::create(2, 2, Color::white());
  const std::string key = flat_save_ack_key("/tmp/a.png", ImageFormat::Png, true, false);
  expect(!doc->flat_save_acked(key), "new document has no ack");
  doc->ack_flat_save(key);
  expect(doc->flat_save_acked(key), "ack sticks");
  doc->ack_flat_save(key);
  expect(doc->flat_save_acked(key), "ack twice is still acked");
}

void test_jpeg_quality_predicate() {
  expect(!save_shows_jpeg_quality("untitled.ora", ImageFormat::Ora), "ora hides quality");
  expect(!save_shows_jpeg_quality("x.png", ImageFormat::Png), "png hides quality");
  expect(!save_shows_jpeg_quality("x.png", ImageFormat::Jpeg), "typed png wins over jpeg filter");
  expect(!save_shows_jpeg_quality("x.bmp", ImageFormat::Jpeg), "typed bmp hides quality");
  expect(save_shows_jpeg_quality("x.jpg", ImageFormat::Png), "typed jpg shows quality");
  expect(save_shows_jpeg_quality("x.jpeg", ImageFormat::Ora), "typed jpeg shows quality");
  expect(save_shows_jpeg_quality("photo", ImageFormat::Jpeg), "no extension follows the jpeg filter");
  expect(!save_shows_jpeg_quality("photo", ImageFormat::Png), "no extension follows the png filter");
  expect(save_shows_jpeg_quality("", ImageFormat::Jpeg), "empty name follows the jpeg filter");
  expect(!save_shows_jpeg_quality("", ImageFormat::Ora), "empty name follows the ora filter");
}

void test_undo() {
  expect(std::strcmp(kUndoDroppedNotice, "Older undo steps were dropped to save memory") == 0,
         "notice text");
  const std::size_t budget = suggested_undo_bytes();
  expect(budget >= kDefaultUndoBytes, "undo budget is at least 256 MB");
  expect(budget <= 512ull * 1024ull * 1024ull, "undo budget stays within 512 MB");

  auto doc = Document::create(32, 32, Color::white());
  expect(std::strcmp(doc->history().base_label(), "New document") == 0, "fresh history is the new document");
  expect(!doc->history().base_dropped(), "nothing dropped yet");
  commit_checker(*doc, 1, "first pattern");
  expect(doc->history().count() == 1, "a step larger than a 1-byte cap is still kept");
  doc->history().set_byte_cap(1);
  expect(doc->history().count() == 1, "trimming does not drop the only step");
  expect(doc->history().can_undo(), "the oversized step can be undone");
  Layer& layer = doc->layers().active_layer();
  const Color baked = layer.pixel(0, 0);
  expect(!(baked == Color::white()), "the first pattern is on the canvas");
  commit_checker(*doc, 2, "second pattern");
  expect(doc->history().count() == 1, "the cap drops the older step");
  expect(doc->history().name_at(0) == "second pattern", "the newest step remains");
  expect(doc->history().base_dropped(), "the base is no longer the new document");
  expect(std::strcmp(doc->history().base_label(), "Start of history") == 0, "row 0 is honest");
  expect(doc->history().consume_drop_notice(), "the drop is announced once");
  expect(!doc->history().consume_drop_notice(), "the announcement is not repeated");

  doc->history().undo(*doc);
  expect(doc->history().index() == -1, "undo lands on the oldest restorable state");
  expect(layer.pixel(0, 0) == baked, "that state is the baked first pattern, not a blank canvas");
  expect(!(layer.pixel(0, 0) == Color::white()), "the canvas is not the original white document");
  doc->history().redo(*doc);
  expect(doc->history().name_at(0) == "second pattern", "redo restores the kept step");
  expect(!(layer.pixel(0, 0) == baked), "redo paints the second pattern");
}

void test_undo_4000() {
  auto doc = Document::create(4000, 4000, Color::white());
  expect(doc->history().byte_cap() == kDefaultUndoBytes, "a document starts at the 256 MB floor");
  commit_checker(*doc, 1, "fill a");
  commit_checker(*doc, 2, "fill b");
  commit_checker(*doc, 3, "fill c");
  expect(doc->history().count() > 2, "a 4000 px picture keeps more than two full-canvas steps");
  expect(!doc->history().base_dropped(), "those steps fit, so history still starts at the new document");
  expect(doc->history().memory_bytes() <= kDefaultUndoBytes, "the steps fit in the budget");
  expect(doc->history().memory_bytes() < 32ull * 1024ull * 1024ull,
         "full-canvas pattern steps are stored compressed");

  Layer& layer = doc->layers().active_layer();
  const Color painted = layer.pixel(0, 0);
  doc->jump_history(-1);
  expect(doc->history().index() == -1, "undo to the start");
  expect(layer.pixel(0, 0) == Color::white(), "the start really is the white document");
  expect(layer.pixel(100, 80) == Color::white(), "a later pixel is white too");
  doc->jump_history(doc->history().count() - 1);
  expect(layer.pixel(0, 0) == painted, "redo restores the 4000 px pattern");
}

}  // namespace

int main() {
  test_modes();
  test_companion_and_warnings();
  test_jpeg_quality_predicate();
  test_undo();
  test_undo_4000();
  if (errors != 0) {
    std::fprintf(stderr, "test_review_r7: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_review_r7: ok\n");
  return 0;
}
