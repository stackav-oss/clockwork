// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/alignment_type.hh"
#include "clockwork/dial/approx_aligner.hh"
#include "clockwork/dial/approx_aligner_config.hh"
#include "clockwork/dial/approx_aligner_policies.hh"
#include "clockwork/examples/approx_aligner/approx_aligner_cog_dial.hh"
#include "clockwork/examples/approx_aligner/approx_aligner_msgs.hh"
#include "clockwork/pinion/publisher_handle.hh"
#include "jewels/container/circular_buffer.hh"
#include "jewels/memory/memory_resource.hh"
#include "jewels/memory/pointers.hh"

#include <boost/iterator/iterator_facade.hpp>

#include <optional>
#include <tuple>
#include <type_traits>

namespace clockwork
{

template <typename... InputPolicies>
struct AlignerPolicy : ApproxAlignerPolicies<InputPolicies...>
{
  static constexpr auto validate_inputs = ApproxAlignerPolicies<InputPolicies...>::monotonically_increasing_inputs;
  static constexpr auto less_than = ApproxAlignerPolicies<InputPolicies...>::array_less_than;
  static constexpr auto objective = ApproxAlignerPolicies<InputPolicies...>::exact_alignment_objective;
};

using IntInput = TovNanosecondsApproxAlignerInput<
  std::decay_t<std::result_of_t<decltype (&ApproxAlignerCogDialInputs::get_in_int)(ApproxAlignerCogDialInputs)>>>;
using FloatInput = TovNanosecondsApproxAlignerInput<
  std::decay_t<std::result_of_t<decltype (&ApproxAlignerCogDialInputs::get_in_float)(ApproxAlignerCogDialInputs)>>>;
using Aligner = ApproxAligner<AlignerPolicy, IntInput, FloatInput>;

void execute_cog(ApproxAlignerCogDial& dial)
{
  const auto& now = dial.get_start_time();
  const auto& resource = dial.get_resources().get_aligner_resource();
  const auto& config = dial.get_configs().get_aligner_config();
  auto& state = dial.get_states().get_aligner_state();
  auto inputs = Aligner::InputTuple(dial.get_inputs().get_in_int(), dial.get_inputs().get_in_float());

  // Call the aligner.

  auto result = Aligner::find_alignment(resource, config, state, inputs, now);

  // Handle the results accordingly.

  switch (result.state)
  {
  case ApproxAlignerStateType::aligned:
    switch (result.type)
    {
    case AlignmentType::full:
      if (result.alignment)
      {
        const auto& aligned_inputs = result.alignment->inputs;
        const auto& aligned_int = std::get<0>(aligned_inputs);
        const auto& aligned_float = std::get<1>(aligned_inputs);

        // Publish output message.

        auto& output = dial.get_outputs().get_out_aligned().message();
        output.set_time_of_validity(aligned_int->get_time_of_validity());
        output.set_int_value(aligned_int->get_int_value());
        output.set_float_value(aligned_float->get_float_value());

        dial.get_outputs().get_out_aligned().mark_for_publish();

        // Update the aligner state.

        Aligner::commit(state, inputs, result);
      }
      break;

    case AlignmentType::unspecified:
      [[fallthrough]];
    case AlignmentType::none:
      [[fallthrough]];
    case AlignmentType::partial:
      break;
    }
    break;

  case ApproxAlignerStateType::unspecified:
    [[fallthrough]];
  case ApproxAlignerStateType::idle:
    [[fallthrough]];
  case ApproxAlignerStateType::invalid_inputs:
    [[fallthrough]];
  case ApproxAlignerStateType::insufficient_data:
    [[fallthrough]];
  case ApproxAlignerStateType::timeout:
    break;
  }
}

} // namespace clockwork
