#pragma once

#include <ntv2enums.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace godot {

// Converts a four-byte Godot readback into the selected AJA frame-buffer
// layout. Scratch buffers are retained so steady-state output never allocates.
class AJAVideoConverter {
public:
  bool prepare(int p_width, int p_height, int p_destination_stride,
               size_t p_destination_bytes,
               NTV2FrameBufferFormat p_destination_format);
  bool convert(const uint8_t *p_pixels, bool p_source_bgra,
               void *p_destination, size_t p_destination_bytes);

private:
  int _width = 0;
  int _height = 0;
  int _destination_stride = 0;
  int _padded_pixels = 0;
  size_t _destination_bytes = 0;
  NTV2FrameBufferFormat _format = NTV2_FBF_INVALID;
  std::vector<uint8_t> _argb;
  std::vector<uint8_t> _yuv8;
  std::vector<UWord> _yuv10_line;
};

} // namespace godot
