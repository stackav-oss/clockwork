// Copyright 2025 Stack AV Co.
// SPDX-License-Identifier: Apache-2.0

#include "clockwork/dial/msg_input.hh"
#include "clockwork/examples/demo_system/gps/gps_clk_cc_dial.hh"
#include "clockwork/examples/demo_system/gps/gps_message_clk_cc.hh"
#include "clockwork/pinion/publishable.hh"

#include <ranges>

namespace clockwork::demo_system::gps
{

namespace
{

constexpr auto latitude_deg = 40.4;
constexpr auto longitude_deg = -80.0;
constexpr auto elevation_m = 233.0;

} // namespace

void execute_cog(GpsDeviceCogDial& dial)
{
  // Demo system uses a fixed location.
  auto& raw_msg = dial.get_outputs().get_raw_gps().message();
  raw_msg.set_time_of_validity(dial.get_start_time());
  raw_msg.set_latitude_deg(latitude_deg);
  raw_msg.set_longitude_deg(longitude_deg);
  raw_msg.set_elevation_m(elevation_m);
  dial.get_outputs().get_raw_gps().mark_for_publish();
}

void execute_cog(GpsDriverCogDial& dial)
{
  const auto& raw_msg = dial.get_inputs().get_raw_gps().get_new_msgs_view().back();
  // GPS packet processing would go here.
  dial.get_outputs().get_gps().message() = raw_msg;
  dial.get_outputs().get_gps().mark_for_publish();
}

} // namespace clockwork::demo_system::gps
