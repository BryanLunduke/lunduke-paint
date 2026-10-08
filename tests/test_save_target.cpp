// SPDX-License-Identifier: GPL-3.0-or-later
// Resolver matrix: typed name × save filter. The filter never overrides a
// known extension. It appends one only when the name has no extension.
// A short unknown suffix is offered as "<name>.<filter>", not rewritten.

#include "io/save_target.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace {

using lundukepaint::ImageFormat;
using lundukepaint::SaveResolveKind;
using lundukepaint::SaveTarget;
using lundukepaint::format_extension;
using lundukepaint::name_for_filter_change;
using lundukepaint::resolve_save_target;

int errors = 0;

void expect(bool cond, const std::string& msg) {
  if (!cond) {
    std::fprintf(stderr, "test_save_target: %s\n", msg.c_str());
    ++errors;
  }
}

enum class Kind { Png, Jpeg, Bmp, Ora, Gif, None, Unknown };

struct Row {
  const char* path;
  Kind kind;
};

const ImageFormat kFilters[] = {ImageFormat::Ora, ImageFormat::Png, ImageFormat::Jpeg,
                                ImageFormat::Bmp, ImageFormat::Unknown};

ImageFormat kind_format(Kind kind) {
  switch (kind) {
    case Kind::Png:
      return ImageFormat::Png;
    case Kind::Jpeg:
      return ImageFormat::Jpeg;
    case Kind::Bmp:
      return ImageFormat::Bmp;
    case Kind::Ora:
      return ImageFormat::Ora;
    case Kind::Gif:
      return ImageFormat::Gif;
    default:
      return ImageFormat::Unknown;
  }
}

std::string basename_of(const std::string& path) {
  const auto slash = path.find_last_of("/\\");
  return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string append_expected(const std::string& path, ImageFormat filter) {
  const auto slash = path.find_last_of("/\\");
  const std::string dir = slash == std::string::npos ? std::string() : path.substr(0, slash + 1);
  std::string base = slash == std::string::npos ? path : path.substr(slash + 1);
  if (!base.empty() && base.back() == '.') {
    base.pop_back();
  }
  return dir + base + format_extension(filter);
}

void check_row(const Row& row, ImageFormat filter) {
  const SaveTarget got = resolve_save_target(row.path, filter);
  const std::string label = std::string(row.path) + " x filter " + std::to_string(static_cast<int>(filter));
  const bool writable = filter == ImageFormat::Png || filter == ImageFormat::Jpeg ||
                        filter == ImageFormat::Bmp || filter == ImageFormat::Ora;
  if (row.kind == Kind::Png || row.kind == Kind::Jpeg || row.kind == Kind::Bmp || row.kind == Kind::Ora ||
      row.kind == Kind::Gif) {
    expect(got.kind == SaveResolveKind::Ready, label + " known extension is ready");
    expect(got.format == kind_format(row.kind), label + " format follows the typed extension");
    expect(got.path == row.path, label + " keeps the typed name and case");
    return;
  }
  if (row.kind == Kind::Unknown) {
    if (!writable) {
      expect(got.kind == SaveResolveKind::Unresolved, label + " unknown suffix without a filter is unresolved");
      expect(!got.message.empty(), label + " explains the unknown suffix");
      expect(got.message.find(basename_of(row.path)) != std::string::npos, label + " names the file");
      return;
    }
    const std::string offered = std::string(row.path) + format_extension(filter);
    expect(got.kind == SaveResolveKind::PromptAppend, label + " unknown suffix asks instead of rewriting");
    expect(got.offered_format == filter, label + " offer uses the filter format");
    expect(got.offered_path == offered, label + " offer appends the filter extension");
    expect(got.offered_path != lundukepaint::replace_path_extension(row.path, format_extension(filter)),
           label + " does not strip the unknown suffix");
    expect(got.message.find(basename_of(offered)) != std::string::npos, label + " message shows the offered name");
    expect(got.path.empty(), label + " does not pretend the original path is ready");
    return;
  }
  if (!writable) {
    expect(got.kind == SaveResolveKind::Unresolved, label + " no extension and no filter is unresolved");
    expect(!got.message.empty(), label + " asks for a file type");
    return;
  }
  expect(got.kind == SaveResolveKind::Ready, label + " missing extension is filled from the filter");
  expect(got.format == filter, label + " appended format is the filter");
  expect(got.path == append_expected(row.path, filter), label + " appends without stripping a dotted title");
}

}  // namespace

int main() {
  const SaveTarget bug = resolve_save_target("/tmp/work/bft.png", ImageFormat::Ora);
  expect(bug.kind == SaveResolveKind::Ready, "bft.png with the OpenRaster filter is ready");
  expect(bug.format == ImageFormat::Png, "bft.png is saved as PNG");
  expect(bug.path == "/tmp/work/bft.png", "bft.png is not rewritten to bft.ora");

  const SaveTarget upper = resolve_save_target("scan.PNG", ImageFormat::Ora);
  expect(upper.kind == SaveResolveKind::Ready && upper.format == ImageFormat::Png && upper.path == "scan.PNG",
         ".PNG keeps the user's case and is PNG");
  const SaveTarget jpeg = resolve_save_target("scan.Jpeg", ImageFormat::Png);
  expect(jpeg.kind == SaveResolveKind::Ready && jpeg.format == ImageFormat::Jpeg && jpeg.path == "scan.Jpeg",
         ".Jpeg wins over the PNG filter and keeps its case");

  const Row rows[] = {
      {"bft.png", Kind::Png},
      {"bft.PNG", Kind::Png},
      {"bft.jpg", Kind::Jpeg},
      {"bft.JPG", Kind::Jpeg},
      {"bft.jpeg", Kind::Jpeg},
      {"bft.Jpeg", Kind::Jpeg},
      {"bft.bmp", Kind::Bmp},
      {"bft.BMP", Kind::Bmp},
      {"bft.ora", Kind::Ora},
      {"bft.ORA", Kind::Ora},
      {"bft.gif", Kind::Gif},
      {"bft.GIF", Kind::Gif},
      {"a.ora.png", Kind::Png},
      {"file.jpeg.BMP", Kind::Bmp},
      {"my.drawing.JPEG", Kind::Jpeg},
      {"/home/me.backup/bft.png", Kind::Png},
      {"/home/me.backup/portrait.ora", Kind::Ora},
      {"bft", Kind::None},
      {"my.drawing", Kind::None},
      {"notes.backup", Kind::None},
      {"bft.", Kind::None},
      {".hidden", Kind::None},
      {"/home/me.backup/portrait", Kind::None},
      {"bft.xyz", Kind::Unknown},
      {"notes.txt", Kind::Unknown},
      {"a.ora.xyz", Kind::Unknown},
      {"file.webp", Kind::Unknown},
      {"BFT.Png", Kind::Png},
  };

  for (const Row& row : rows) {
    for (ImageFormat filter : kFilters) {
      check_row(row, filter);
    }
  }

  for (ImageFormat filter : kFilters) {
    const SaveTarget empty = resolve_save_target("", filter);
    expect(empty.kind == SaveResolveKind::Unresolved, "an empty name is unresolved");
    const SaveTarget dir = resolve_save_target("/tmp/", filter);
    expect(dir.kind == SaveResolveKind::Unresolved, "a trailing slash has no file name");
  }

  const SaveTarget trimmed = resolve_save_target("  bft.png  ", ImageFormat::Ora);
  expect(trimmed.kind == SaveResolveKind::Ready && trimmed.path == "bft.png" && trimmed.format == ImageFormat::Png,
         "spaces around the file name are removed");

  const SaveTarget dotted = resolve_save_target("/home/me.backup/my.drawing", ImageFormat::Ora);
  expect(dotted.kind == SaveResolveKind::Ready && dotted.path == "/home/me.backup/my.drawing.ora" &&
             dotted.format == ImageFormat::Ora,
         "a dotted title gets the filter extension appended");
  const SaveTarget trail = resolve_save_target("bft.", ImageFormat::Jpeg);
  expect(trail.path == "bft.jpg" && trail.format == ImageFormat::Jpeg, "a trailing dot is replaced by .jpg");
  const SaveTarget bare = resolve_save_target("bft", ImageFormat::Jpeg);
  expect(bare.path == "bft.jpg" && bare.format == ImageFormat::Jpeg, "JPEG's canonical extension is .jpg");

  expect(name_for_filter_change("bft.png", ImageFormat::Ora, ImageFormat::Jpeg) == "bft.png",
         "changing the filter does not clobber a typed .png");
  expect(name_for_filter_change("untitled.ora", ImageFormat::Ora, ImageFormat::Png) == "untitled.png",
         "the filter may replace the extension it owns");
  expect(name_for_filter_change("photo.jpeg", ImageFormat::Jpeg, ImageFormat::Bmp) == "photo.bmp",
         ".jpeg belongs to the JPEG filter");
  expect(name_for_filter_change("photo.JPG", ImageFormat::Jpeg, ImageFormat::Png) == "photo.png",
         ".JPG belongs to the JPEG filter");
  expect(name_for_filter_change("bft.xyz", ImageFormat::Ora, ImageFormat::Png) == "bft.xyz",
         "an unknown suffix is not rewritten when the filter changes");
  expect(name_for_filter_change("my.drawing", ImageFormat::Ora, ImageFormat::Png) == "my.drawing.png",
         "a dotted title with no extension takes the new filter extension");
  expect(name_for_filter_change("bft", ImageFormat::Ora, ImageFormat::Bmp) == "bft.bmp",
         "a bare name takes the new filter extension");
  expect(name_for_filter_change("bft.", ImageFormat::Png, ImageFormat::Ora) == "bft.ora",
         "a trailing dot takes the new filter extension");
  expect(name_for_filter_change("untitled.png", ImageFormat::Png, ImageFormat::Png) == "untitled.png",
         "the same filter leaves the name alone");

  if (errors != 0) {
    std::fprintf(stderr, "test_save_target: %d failure(s)\n", errors);
    return 1;
  }
  std::printf("test_save_target: ok (%d names x %d filters)\n", static_cast<int>(sizeof(rows) / sizeof(rows[0])),
              static_cast<int>(sizeof(kFilters) / sizeof(kFilters[0])));
  return 0;
}
