// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_IMAGE_IO_HPP
#define LUNDUKEPAINT_IO_IMAGE_IO_HPP

#include "raster/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>
#include <vector>

namespace lundukepaint {

struct LoadedImage {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
  std::string layer_name;
  std::string error;
  // True when the file is an animation and only the first frame was loaded.
  bool animated = false;
  bool ok() const { return error.empty() && width > 0 && height > 0; }
};

enum class ImageFormat { Png, Jpeg, Bmp, Ora, Gif, Unknown };

ImageFormat format_from_path(const std::string& path);
std::string format_extension(ImageFormat format);

// Replace the extension of the final path component. A dot in a parent
// directory is left alone. `extension` may or may not start with '.'.
// "/home/me.backup/portrait" + ".png" -> "/home/me.backup/portrait.png".
std::string replace_path_extension(const std::string& path, const std::string& extension);

// After a flat file has been written, "Flatten and keep .ora" must not mark
// the document clean when the layered copy failed.
struct KeepOraDecision {
  bool adopt_path = false;
  bool mark_clean = false;
  bool clear_recovery = false;
};

inline KeepOraDecision decide_keep_ora(bool flat_ok, bool ora_requested, bool ora_ok) {
  KeepOraDecision decision;
  if (!flat_ok || (ora_requested && !ora_ok)) {
    return decision;
  }
  decision.adopt_path = true;
  decision.mark_clean = true;
  decision.clear_recovery = true;
  return decision;
}

// True when the loader reports more than one frame (animated GIF).
bool image_has_multiple_frames(const std::string& path);

// Exclusive temp file in the destination directory. A final symlink is
// resolved (realpath) and the real file is replaced in its own directory, so
// the link stays a link (round 15 P-1); a dangling link is refused with a
// hint to use Save As. commit fsyncs and renames over the real file.
// abort unlinks the temp. Stale temps of the same base name left by a crashed
// Paint are removed first (round 15 P-3).
struct AtomicFile {
  int fd = -1;
  std::string tmp_path;
  std::string dest_path;
};

bool atomic_create(const std::string& path, AtomicFile& out, std::string& error);
// Exposed for tests. Temps younger than this are never removed.
constexpr int kStaleSaveTempSeconds = 600;
bool is_stale_save_temp_name(const std::string& name, const std::string& base, pid_t* pid_out);
int remove_stale_save_temps(const std::string& dir, const std::string& base, int min_age_seconds);
bool atomic_commit(AtomicFile& file, std::string& error);
void atomic_abort(AtomicFile& file);

LoadedImage load_flat_image(const std::string& path);

// 0.9.5-2: the pixel size from the file header only (gdk_pixbuf_get_file_info),
// with no full decode, so Open can show the large-image warning first and
// skip the decode on Cancel. False when the header does not give a size.
bool flat_image_header_size(const std::string& path, int& width, int& height);
// Number of full decodes load_flat_image has started in this process.
// Lets tests check that a cancelled large-image warning decoded nothing.
int flat_image_decode_count();

// One scaled preview of a PNG, JPEG, BMP, GIF, or OpenRaster file.
// `max_edge` is the longer-side cap (no upscale). Returns false for anything
// that is not a readable image of those types; `out.error` may be set and
// must not be shown as a dialog.
bool load_image_preview(const std::string& path, int max_edge, LoadedImage& out);

bool save_flat_image(const std::string& path, ImageFormat format, const std::uint8_t* rgba,
                     int width, int height, int stride, int jpeg_quality, std::string& error);

bool encode_png_memory(const std::uint8_t* rgba, int width, int height, int stride,
                       std::vector<std::uint8_t>& out, std::string& error);
bool png_header_size_ok(const std::uint8_t* data, std::size_t size);
bool decode_png_memory(const std::uint8_t* data, std::size_t size, LoadedImage& out);

// Clipboard images (round 10, P-1). Read only the encoded header to learn the
// size, so an oversized clipboard image is refused before any pixels are
// decoded. `mime_type` names the clipboard target ("image/png"); an empty or
// unknown type lets the loader sniff the data. Returns false when the data is
// not a readable image header.
bool encoded_image_size(const std::uint8_t* data, std::size_t size, const std::string& mime_type,
                        int& width, int& height);

// Full decode of an encoded image held in memory. `out_of_memory` is set when
// the failure was an allocation failure, so the caller can say so instead of
// "no image".
bool decode_encoded_image(const std::uint8_t* data, std::size_t size, const std::string& mime_type,
                          LoadedImage& out, bool& out_of_memory);

}  // namespace lundukepaint

#endif
