// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/approx_aligner.hh"
#include "clockwork/dial/approx_aligner_config.hh"
#include "clockwork/dial/approx_aligner_policies.hh"
#include "clockwork/examples/demo_system/camera/video_message.hh"
#include "clockwork/examples/demo_system/lidar/lidar_message.hh"
#include "clockwork/examples/demo_system/localization/pose_message.hh"
#include "clockwork/examples/demo_system/perception/perception_cog_dial.hh"
#include "clockwork/examples/demo_system/perception/perception_message.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <algorithm>
#include <chrono>
#include <optional>
#include <tuple>
#include <type_traits>

namespace clockwork::demo_system::perception
{

namespace
{

// Approx aligner policies
template <typename... InputPolicies>
struct AlignerPolicy : ApproxAlignerPolicies<InputPolicies...>
{
  static constexpr auto validate_inputs = ApproxAlignerPolicies<InputPolicies...>::monotonically_increasing_inputs;
  static constexpr auto less_than = ApproxAlignerPolicies<InputPolicies...>::array_less_than;
  static constexpr auto objective = ApproxAlignerPolicies<InputPolicies...>::variance_objective;
};

// Approx aligner pose input
using PoseInput = TovNanosecondsApproxAlignerInput<
  std::decay_t<std::result_of_t<decltype (&PerceptionCogDialInputs::get_pose)(PerceptionCogDialInputs)>>>;
constexpr auto pose_index = 0U;

// Approx aligner lidar input
using LidarInput = TovNanosecondsApproxAlignerInput<
  std::decay_t<std::result_of_t<decltype (&PerceptionCogDialInputs::get_lidar)(PerceptionCogDialInputs)>>>;
constexpr auto lidar_index = 1U;

// Approx aligner video input
using VideoInput = TovNanosecondsApproxAlignerInput<
  std::decay_t<std::result_of_t<decltype (&PerceptionCogDialInputs::get_video)(PerceptionCogDialInputs)>>>;
constexpr auto video_index = 2U;

// Aprox aligner for pose, lidar and video
using Aligner = ApproxAligner<AlignerPolicy, PoseInput, LidarInput, VideoInput>;

} // namespace

// Publish the approx aligner config to a persistent channel
void execute_cog(PerceptionInitCogDial& dial)
{
  dial.get_outputs().get_aligner_config_out().message() = dial.get_configs().get_aligner_config();
  dial.get_outputs().get_aligner_config_out().mark_for_publish();
}

// Use an approx aligner to get pose, lidar, and camera at about the same time and
// publish a debug message with timestamps from each input and the spread between them.
void execute_cog(PerceptionCogDial& dial)
{
  const auto start_time = dial.get_start_time();
  const auto& memory = dial.get_resources().get_memory();
  const auto& aligner_config = dial.get_configs().get_aligner_config();
  auto& aligner_state = dial.get_states().get_aligner_state();
  auto inputs =
    Aligner::InputTuple(dial.get_inputs().get_pose(), dial.get_inputs().get_lidar(), dial.get_inputs().get_video());
  auto result = Aligner::find_alignment(memory, aligner_config, aligner_state, inputs, start_time);
  if (
    (result.state == ApproxAlignerStateType::aligned || result.state == ApproxAlignerStateType::timeout) &&
    result.alignment)
  {
    const auto& aligned_inputs = result.alignment->inputs;
    const auto aligned_pose = std::get<pose_index>(aligned_inputs);
    const auto aligned_lidar = std::get<lidar_index>(aligned_inputs);
    const auto aligned_video = std::get<video_index>(aligned_inputs);
    auto& debug_msg = dial.get_outputs().get_debug().message();
    debug_msg.set_time_of_validity(dial.get_start_time());
    debug_msg.set_video_tov(aligned_video->get_time_of_validity());
    debug_msg.set_lidar_tov(aligned_lidar->get_time_of_validity());
    debug_msg.set_pose_tov(aligned_pose->get_time_of_validity());
    debug_msg.set_tov_spread(
      std::max({debug_msg.get_video_tov(), debug_msg.get_lidar_tov(), debug_msg.get_pose_tov()}) -
      std::min({debug_msg.get_video_tov(), debug_msg.get_lidar_tov(), debug_msg.get_pose_tov()}));
    dial.get_outputs().get_debug().mark_for_publish();
    Aligner::commit(aligner_state, inputs, result);
  }
}

} // namespace clockwork::demo_system::perception
