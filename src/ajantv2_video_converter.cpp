#include "ajantv2_video_converter.hpp"

#include <ntv2utils.h>

#include <libyuv/convert_argb.h>
#include <libyuv/convert_from_argb.h>

#include <cstring>
#include <limits>

using namespace godot;

bool AJAVideoConverter::prepare(
    int p_width, int p_height, int p_destination_stride,
    size_t p_destination_bytes, NTV2FrameBufferFormat p_destination_format) {
  _width = 0;
  _height = 0;
  _destination_stride = 0;
  _padded_pixels = 0;
  _destination_bytes = 0;
  _format = NTV2_FBF_INVALID;
  _argb.clear();
  _yuv8.clear();
  _yuv10_line.clear();

  if (p_width <= 0 || p_height <= 0 || p_destination_stride <= 0 ||
      p_destination_bytes <
          static_cast<size_t>(p_destination_stride) * p_height ||
      p_width > std::numeric_limits<int>::max() / 4 ||
      (p_destination_format != NTV2_FBF_ABGR &&
       p_destination_format != NTV2_FBF_8BIT_YCBCR &&
       p_destination_format != NTV2_FBF_10BIT_YCBCR)) {
    return false;
  }

  _width = p_width;
  _height = p_height;
  _destination_stride = p_destination_stride;
  _destination_bytes = p_destination_bytes;
  _format = p_destination_format;

  if (_format == NTV2_FBF_ABGR) {
    return _destination_stride >= _width * 4;
  }

  // libyuv names formats by the little-endian 32-bit word. Godot RGBA bytes
  // are therefore ABGR to libyuv and first need swapping to its ARGB layout.
  _argb.resize(static_cast<size_t>(_width) * _height * 4);
  if (_format == NTV2_FBF_8BIT_YCBCR) {
    return _destination_stride >= _width * 2;
  }

  // AJA 10-bit YCbCr uses v210: six pixels occupy four 32-bit words and each
  // row is padded to a complete group.
  if (_destination_stride % (4 * static_cast<int>(sizeof(ULWord))) != 0) {
    return false;
  }
  _padded_pixels =
      _destination_stride / (4 * static_cast<int>(sizeof(ULWord))) * 6;
  if (_padded_pixels < _width) {
    return false;
  }

  _yuv8.resize(static_cast<size_t>(_width) * _height * 2);
  _yuv10_line.resize(static_cast<size_t>(_padded_pixels) * 2);
  for (int pixel = 0; pixel < _padded_pixels; pixel += 2) {
    const size_t sample = static_cast<size_t>(pixel) * 2;
    _yuv10_line[sample] = 512;
    _yuv10_line[sample + 1] = 64;
    _yuv10_line[sample + 2] = 512;
    _yuv10_line[sample + 3] = 64;
  }
  return true;
}

bool AJAVideoConverter::convert(const uint8_t *p_pixels, bool p_source_bgra,
                                void *p_destination,
                                size_t p_destination_bytes) {
  if (!p_pixels || !p_destination ||
      p_destination_bytes < _destination_bytes) {
    return false;
  }

  if (_format == NTV2_FBF_ABGR) {
    if (!p_source_bgra) {
      for (int row = 0; row < _height; ++row) {
        std::memcpy(static_cast<uint8_t *>(p_destination) +
                        static_cast<size_t>(row) * _destination_stride,
                    p_pixels + static_cast<size_t>(row) * _width * 4,
                    static_cast<size_t>(_width) * 4);
      }
      return true;
    }
    return libyuv::ARGBToABGR(
               p_pixels, _width * 4, static_cast<uint8_t *>(p_destination),
               _destination_stride, _width, _height) == 0;
  }

  const uint8_t *argb = p_pixels;
  if (!p_source_bgra) {
    if (_argb.empty() ||
        libyuv::ABGRToARGB(p_pixels, _width * 4, _argb.data(), _width * 4,
                           _width, _height) != 0) {
      return false;
    }
    argb = _argb.data();
  }

  if (_format == NTV2_FBF_8BIT_YCBCR) {
    return libyuv::ARGBToUYVY(
               argb, _width * 4, static_cast<uint8_t *>(p_destination),
               _destination_stride, _width, _height) == 0;
  }

  if (_format != NTV2_FBF_10BIT_YCBCR || _yuv8.empty() ||
      _yuv10_line.empty() ||
      libyuv::ARGBToUYVY(argb, _width * 4, _yuv8.data(), _width * 2,
                         _width, _height) != 0) {
    return false;
  }

  for (int row = 0; row < _height; ++row) {
    const uint8_t *source =
        _yuv8.data() + static_cast<size_t>(row) * _width * 2;
    for (int sample = 0; sample < _width * 2; ++sample) {
      _yuv10_line[static_cast<size_t>(sample)] =
          static_cast<UWord>(source[sample]) << 2;
    }
    auto *destination = reinterpret_cast<ULWord *>(
        static_cast<uint8_t *>(p_destination) +
        static_cast<size_t>(row) * _destination_stride);
    PackLine_16BitYUVto10BitYUV(_yuv10_line.data(), destination,
                                static_cast<ULWord>(_padded_pixels));
  }
  return true;
}
