// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LUNDUKEPAINT_IO_IMAGE_IO_HPP
#define LUNDUKEPAINT_IO_IMAGE_IO_HPP

#include "raster/types.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
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

// Exclusive temp file in the destination directory. Refuses a final symlink.
// commit fsyncs and renames over `path`. abort unlinks the temp.
struct AtomicFile {
  int fd = -1;
  std::string tmp_path;
  std::string dest_path;
};

bool atomic_create(const std::string& path, AtomicFile& out, std::string& error);
bool atomic_commit(AtomicFile& file, std::string& error);
void atomic_abort(AtomicFile& file);

LoadedImage load_flat_image(const std::string& path);

// One scaled preview of a PNG, JPEG, BMP, GIF, or OpenRaster file.
// `max_edge` is the longer-side cap (no upscale). Returns false for anything
// that is not a readable image of those types; `out.error` may be set and
// must not be shown as a dialog.
bool load_image_preview(const std::string& path, int max_edge, LoadedImage& out);

bool save_flat_image(const std::string& path, ImageFormat format, const std::uint8_t* rgba,
                     int width, int height, int stride, int jpeg_quality, std::string& error);

bool encode_png_memory(const std::uint8_t* rgba, int width, int height, int stride,
                       std::vector<std::uint8_t>& out, std::string& error);
bool decode_png_memory(const std::uint8_t* data, std::size_t size, LoadedImage& out);

}  // namespace lundukepaint

#endif
