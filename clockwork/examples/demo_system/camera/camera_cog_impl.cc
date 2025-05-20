// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/demo_system/camera/camera_cog_dial.hh"
#include "clockwork/examples/demo_system/camera/video_message.hh"
#include "clockwork/pinion/publisher_handle.hh"

#include <cstddef>
#include <ranges>
#include <span>

namespace clockwork::demo_system::camera
{

// Sanity checks
static_assert(raw_image_size_bytes == image_width_pixels * image_height_pixels * 3U / 2U);
static_assert(image_size_bytes == image_width_pixels * image_height_pixels * 3U);
static_assert(image_width_pixels % 4U == 0U);

void execute_cog(CameraDeviceCogDial& dial)
{
  // Filling the messages with white noise for demo purposes.
  auto& state = dial.get_states().get_state();
  dial.get_outputs().get_raw_camera().message().set_time_of_validity(dial.get_start_time());
  for (auto& element : dial.get_outputs().get_raw_camera().message().get_mutable_data())
  {
    element = state.generate_noise();
  }
  dial.get_outputs().get_raw_camera().mark_for_publish();
}

void execute_cog(CameraDriverCogDial& dial)
{
  // Do the conversion on the CPU for demo purposes.
  const auto& raw_image = dial.get_inputs().get_raw_camera().get_new_msgs_view().back();
  const auto raw_image_data = raw_image.get_data();
  dial.get_outputs().get_camera().message().set_time_of_validity(raw_image.get_time_of_validity());
  auto image_data = dial.get_outputs().get_camera().message().get_mutable_data();
  size_t raw_offset = 0U;
  for (size_t offset = 0U; offset < image_data.size(); offset += yuv444_chunk_size, raw_offset += yuv411_chunk_size)
  {
    image_data[offset + yuv444_u1_offset] = raw_image_data[raw_offset + yuv411_u_offset];
    image_data[offset + yuv444_y1_offset] = raw_image_data[raw_offset + yuv411_y1_offset];
    image_data[offset + yuv444_v1_offset] = raw_image_data[raw_offset + yuv411_v_offset];
    image_data[offset + yuv444_u2_offset] = raw_image_data[raw_offset + yuv411_u_offset];
    image_data[offset + yuv444_y2_offset] = raw_image_data[raw_offset + yuv411_y2_offset];
    image_data[offset + yuv444_v2_offset] = raw_image_data[raw_offset + yuv411_v_offset];
    image_data[offset + yuv444_u3_offset] = raw_image_data[raw_offset + yuv411_u_offset];
    image_data[offset + yuv444_y3_offset] = raw_image_data[raw_offset + yuv411_y3_offset];
    image_data[offset + yuv444_v3_offset] = raw_image_data[raw_offset + yuv411_v_offset];
    image_data[offset + yuv444_u4_offset] = raw_image_data[raw_offset + yuv411_u_offset];
    image_data[offset + yuv444_y4_offset] = raw_image_data[raw_offset + yuv411_y4_offset];
    image_data[offset + yuv444_v4_offset] = raw_image_data[raw_offset + yuv411_v_offset];
  }
  dial.get_outputs().get_camera().mark_for_publish();
}

} // namespace clockwork::demo_system::camera
