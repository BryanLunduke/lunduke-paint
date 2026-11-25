// SPDX-License-Identifier: GPL-3.0-or-later

#include "io/image_io.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace lundukepaint {
namespace {

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
};

struct Box {
  int begin = 0;
  int end = 0;
};

struct Counted {
  Rgb color;
  int count = 0;
};

int channel_range(const std::vector<Counted>& colors, int begin, int end, int channel) {
  int lo = 255;
  int hi = 0;
  for (int i = begin; i < end; ++i) {
    const int v = channel == 0 ? colors[i].color.r : (channel == 1 ? colors[i].color.g : colors[i].color.b);
    lo = std::min(lo, v);
    hi = std::max(hi, v);
  }
  return hi - lo;
}

void median_cut(std::vector<Counted>& colors, int limit) {
  if (static_cast<int>(colors.size()) <= limit) {
    return;
  }
  std::vector<Box> boxes{{0, static_cast<int>(colors.size())}};
  while (static_cast<int>(boxes.size()) < limit) {
    int best = -1;
    int best_range = -1;
    int best_channel = 0;
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
      if (boxes[i].end - boxes[i].begin < 2) {
        continue;
      }
      for (int c = 0; c < 3; ++c) {
        const int range = channel_range(colors, boxes[i].begin, boxes[i].end, c);
        if (range > best_range) {
          best_range = range;
          best = i;
          best_channel = c;
        }
      }
    }
    if (best < 0) {
      break;
    }
    Box box = boxes[best];
    std::sort(colors.begin() + box.begin, colors.begin() + box.end,
              [best_channel](const Counted& a, const Counted& b) {
                const int av = best_channel == 0 ? a.color.r : (best_channel == 1 ? a.color.g : a.color.b);
                const int bv = best_channel == 0 ? b.color.r : (best_channel == 1 ? b.color.g : b.color.b);
                return av < bv;
              });
    int total = 0;
    for (int i = box.begin; i < box.end; ++i) {
      total += colors[i].count;
    }
    int acc = 0;
    int mid = box.begin;
    for (int i = box.begin; i < box.end - 1; ++i) {
      acc += colors[i].count;
      mid = i + 1;
      if (acc >= total / 2) {
        break;
      }
    }
    if (mid <= box.begin || mid >= box.end) {
      mid = box.begin + (box.end - box.begin) / 2;
    }
    boxes[best].end = mid;
    boxes.push_back(Box{mid, box.end});
  }
  std::vector<Counted> reduced;
  reduced.reserve(boxes.size());
  for (const Box& box : boxes) {
    long r = 0;
    long g = 0;
    long b = 0;
    int count = 0;
    for (int i = box.begin; i < box.end; ++i) {
      r += static_cast<long>(colors[i].color.r) * colors[i].count;
      g += static_cast<long>(colors[i].color.g) * colors[i].count;
      b += static_cast<long>(colors[i].color.b) * colors[i].count;
      count += colors[i].count;
    }
    if (count < 1) {
      continue;
    }
    reduced.push_back(Counted{Rgb{static_cast<std::uint8_t>(r / count),
                                  static_cast<std::uint8_t>(g / count),
                                  static_cast<std::uint8_t>(b / count)},
                              count});
  }
  colors.swap(reduced);
}

int nearest_rgb(Rgb color, const std::vector<Rgb>& palette, int first, int last) {
  int best = first;
  int best_d = 1 << 30;
  for (int i = first; i < last; ++i) {
    const int dr = static_cast<int>(color.r) - palette[i].r;
    const int dg = static_cast<int>(color.g) - palette[i].g;
    const int db = static_cast<int>(color.b) - palette[i].b;
    const int d = dr * dr + dg * dg + db * db;
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return best;
}

struct BitPacker {
  int acc = 0;
  int bits = 0;
  std::vector<std::uint8_t> bytes;

  void write(int code, int size) {
    acc |= code << bits;
    bits += size;
    while (bits >= 8) {
      bytes.push_back(static_cast<std::uint8_t>(acc & 0xff));
      acc >>= 8;
      bits -= 8;
    }
  }

  void flush() {
    if (bits > 0) {
      bytes.push_back(static_cast<std::uint8_t>(acc & 0xff));
      acc = 0;
      bits = 0;
    }
  }
};

void lzw_gif(const std::uint8_t* indices, int count, int min_code_size, std::vector<std::uint8_t>& out) {
  const int clear_code = 1 << min_code_size;
  const int eoi = clear_code + 1;
  int code_size = min_code_size + 1;
  int next_code = eoi + 1;
  std::unordered_map<std::uint32_t, int> dict;
  dict.reserve(4096);
  auto reset = [&]() {
    dict.clear();
    code_size = min_code_size + 1;
    next_code = eoi + 1;
  };
  BitPacker bits;
  bits.write(clear_code, code_size);
  if (count < 1) {
    bits.write(eoi, code_size);
    bits.flush();
  } else {
    int prefix = indices[0];
    for (int i = 1; i < count; ++i) {
      const int k = indices[i];
      const std::uint32_t key = (static_cast<std::uint32_t>(prefix) << 8) | static_cast<std::uint32_t>(k);
      const auto found = dict.find(key);
      if (found != dict.end()) {
        prefix = found->second;
        continue;
      }
      bits.write(prefix, code_size);
      if (next_code < 4096) {
        dict.emplace(key, next_code);
        ++next_code;
        if (next_code >= (1 << code_size) && code_size < 12) {
          ++code_size;
        }
      } else {
        bits.write(clear_code, code_size);
        reset();
      }
      prefix = k;
    }
    bits.write(prefix, code_size);
    bits.write(eoi, code_size);
    bits.flush();
  }
  const std::uint8_t* data = bits.bytes.data();
  int left = static_cast<int>(bits.bytes.size());
  while (left > 0) {
    const int n = std::min(255, left);
    out.push_back(static_cast<std::uint8_t>(n));
    out.insert(out.end(), data, data + n);
    data += n;
    left -= n;
  }
  out.push_back(0);
}

void append_u16(std::vector<std::uint8_t>& out, int value) {
  out.push_back(static_cast<std::uint8_t>(value & 0xff));
  out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xff));
}

}  // namespace

bool encode_gif_memory(const std::uint8_t* rgba, int width, int height, int stride,
                       std::vector<std::uint8_t>& out, std::string& error) {
  out.clear();
  if (rgba == nullptr || width < 1 || height < 1 || stride < width * 4) {
    error = "Nothing to save";
    return false;
  }
  std::map<std::uint32_t, int> counts;
  bool any_clear = false;
  for (int y = 0; y < height; ++y) {
    const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      if (p[3] < 128) {
        any_clear = true;
        continue;
      }
      const std::uint32_t key = (static_cast<std::uint32_t>(p[0]) << 16) |
                                (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
      ++counts[key];
    }
  }
  const int color_limit = any_clear ? 255 : 256;
  std::vector<Counted> counted;
  counted.reserve(counts.size());
  for (const auto& entry : counts) {
    Rgb color{static_cast<std::uint8_t>((entry.first >> 16) & 0xff),
              static_cast<std::uint8_t>((entry.first >> 8) & 0xff),
              static_cast<std::uint8_t>(entry.first & 0xff)};
    counted.push_back(Counted{color, entry.second});
  }
  const bool reduced = static_cast<int>(counted.size()) > color_limit;
  if (reduced) {
    median_cut(counted, color_limit);
  }
  std::vector<Rgb> palette;
  palette.push_back(Rgb{0, 0, 0});
  if (!any_clear && !counted.empty()) {
    palette.clear();
  }
  for (const Counted& entry : counted) {
    palette.push_back(entry.color);
  }
  if (palette.empty()) {
    palette.push_back(Rgb{0, 0, 0});
  }
  int bits = 2;
  while ((1 << bits) < static_cast<int>(palette.size())) {
    ++bits;
  }
  if (bits > 8) {
    error = "GIF palette is too large";
    return false;
  }
  const int used = static_cast<int>(palette.size());
  const int table = 1 << bits;
  while (static_cast<int>(palette.size()) < table) {
    palette.push_back(Rgb{0, 0, 0});
  }
  const int first = any_clear ? 1 : 0;
  std::vector<std::uint8_t> indices(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
  std::unordered_map<std::uint32_t, int> exact;
  if (!reduced) {
    for (int i = first; i < static_cast<int>(palette.size()); ++i) {
      const std::uint32_t key = (static_cast<std::uint32_t>(palette[i].r) << 16) |
                                (static_cast<std::uint32_t>(palette[i].g) << 8) | palette[i].b;
      exact.emplace(key, i);
    }
  }
  for (int y = 0; y < height; ++y) {
    const std::uint8_t* row = rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
    for (int x = 0; x < width; ++x) {
      const std::uint8_t* p = row + static_cast<std::size_t>(x) * 4;
      const std::size_t i = static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
      if (p[3] < 128) {
        indices[i] = 0;
        continue;
      }
      if (!reduced) {
        const std::uint32_t key = (static_cast<std::uint32_t>(p[0]) << 16) |
                                  (static_cast<std::uint32_t>(p[1]) << 8) | p[2];
        const auto found = exact.find(key);
        indices[i] = found == exact.end() ? static_cast<std::uint8_t>(first) : static_cast<std::uint8_t>(found->second);
      } else {
        indices[i] = static_cast<std::uint8_t>(nearest_rgb(Rgb{p[0], p[1], p[2]}, palette, first, used));
      }
    }
  }

  out = {'G', 'I', 'F', '8', '9', 'a'};
  append_u16(out, width);
  append_u16(out, height);
  out.push_back(static_cast<std::uint8_t>(0x80 | ((bits - 1) << 4) | (bits - 1)));
  out.push_back(0);
  out.push_back(0);
  for (const Rgb& color : palette) {
    out.push_back(color.r);
    out.push_back(color.g);
    out.push_back(color.b);
  }
  if (any_clear) {
    out.push_back(0x21);
    out.push_back(0xf9);
    out.push_back(4);
    out.push_back(0x01);
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
    out.push_back(0);
  }
  out.push_back(0x2c);
  append_u16(out, 0);
  append_u16(out, 0);
  append_u16(out, width);
  append_u16(out, height);
  out.push_back(0);
  out.push_back(static_cast<std::uint8_t>(bits));
  lzw_gif(indices.data(), width * height, bits, out);
  out.push_back(0x3b);
  return true;
}

}  // namespace lundukepaint
