#pragma once

#include <ntv2card.h>
#include <ntv2devicescanner.h>
#include <ntv2enums.h>
#include <ntv2formatdescriptor.h>
#include <ntv2utils.h>

#include <godot_cpp/core/binder_common.hpp>
#include <godot_cpp/variant/string.hpp>

#include <string>

namespace godot {
namespace aja {

enum PixelFormat {
  PIXEL_FORMAT_AUTO = -1,
  PIXEL_FORMAT_8BIT_YCBCR = NTV2_FBF_8BIT_YCBCR,
  PIXEL_FORMAT_10BIT_YCBCR = NTV2_FBF_10BIT_YCBCR,
  PIXEL_FORMAT_ABGR = NTV2_FBF_ABGR,
};

enum OutputDestination {
  OUTPUT_DESTINATION_AUTO = -1,
};

// App signature used when acquiring exclusive device access.
// 'GAJA' = Godot AJA
static const ULWord kAppSignature = NTV2_FOURCC('G', 'A', 'J', 'A');

inline ::godot::String string_to_godot(const std::string &s) {
  return ::godot::String::utf8(s.c_str());
}

// Returns NTV2Channel for a zero-based channel index.
inline NTV2Channel index_to_channel(int p_index) {
  if (p_index < 0 || p_index >= (int)NTV2_MAX_NUM_CHANNELS) {
    return NTV2_CHANNEL1;
  }
  return static_cast<NTV2Channel>(p_index);
}

// Returns the NTV2InputSource for a given channel (SDI by default).
inline NTV2InputSource channel_to_input_source(NTV2Channel p_channel) {
  return ::NTV2ChannelToInputSource(p_channel, NTV2_IOKINDS_SDI);
}

} // namespace aja
} // namespace godot

VARIANT_ENUM_CAST(godot::aja::PixelFormat);
VARIANT_ENUM_CAST(godot::aja::OutputDestination);
