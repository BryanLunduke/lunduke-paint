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
  bool ok() const { return error.empty() && width > 0 && height > 0; }
};

enum class ImageFormat { Png, Jpeg, Bmp, Ora, Gif, Unknown };

ImageFormat format_from_path(const std::string& path);
std::string format_extension(ImageFormat format);

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
