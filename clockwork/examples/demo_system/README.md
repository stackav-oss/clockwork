# Demo system

This directory contains an autonomy themed example showing how to build and run a [Clockwork](../../docs/README.md) system.
The demo system is only "autonomy themed" and is only intended to provide an example of how to run code under Clockwork.
All of the code used in the example is overly simplified for demo purposes and is not intended to work on an actual vehicle.
For example, the localization code only reports locations and does not provide any information about orientation.
However, the code in the demo system does provide examples on how to use selected Clockwork features which are described in the following sections.

## Running the system

To run the demo system, you first need to create a directory named `/log_root` on your system.
This is the directory where the logs will be written.
Every time the system is run, a new log is created under `/log_root` named using the current unix time in nanoseconds.

Once the logging drive is created, the system is run with a single command.

```bash
bazel run //platforms/clockwork/examples/demo_system
```

The system will run until it is killed by hitting `ctrl-c`.
The system will take about five seconds to stop.
The console output from each cog will be written under `/tmp/simplelaunch_logs`.

While the system is running, you can print messages published by the running cogs by running the channel spy tool.
The channel spy `list-channels` subcommand is used to see what channels are available on the current system.

```bash
$ bazel run //platforms/clockwork/tools/channel_spy:channel_spy_cli -- list-channels
/gps
/imu
/lidar/raw
/lidar/sweep
...
```

The channel spy `echo` subcommand is used to print the messages published on a channel to the console while the system is running.

```bash
$ bazel run //platforms/clockwork/tools/channel_spy:channel_spy_cli -- echo /perception/debug
/perception/debug: 1742772332091220010
{
  "time_of_validity": 1742772332091200120,
  "video_tov": 1742772331892778617,
  "lidar_tov": 1742772331890505331,
  "pose_tov": 1742772331887501886,
  "tov_spread": 5276731
}

/perception/debug: 1742772332291464399
{
  "time_of_validity": 1742772332291430489,
  "video_tov": 1742772332092797397,
  "lidar_tov": 1742772332090508940,
  "pose_tov": 1742772332087513466,
  "tov_spread": 5283931
}
...
```

Clockwork also provides tools for working with logs written by the demo system.

Use the `log_metadata` logging tool to display the metadata for the channels in the log.

```bash
$ bazel run //platforms/clockwork/logging/tools/log_metadata -- -l /log_root/1742772196122282243
/gps
    Type: platforms::clockwork::examples::demo_system::gps::gps_message::GpsMessage
/imu
    Type: platforms::clockwork::examples::demo_system::imu::imu_message::ImuMessage
/lidar/raw
    Type: platforms::clockwork::examples::demo_system::lidar::lidar_message::RawLidarMessage<num_returns=platforms::clockwork::examples::demo_system::lidar::lidar_message::returns_per_message,max_points=40>
...
```

Use the `log_echo` tool to display the contents of a logged channel.

```bash
$ bazel run //platforms/clockwork/logging/tools/echo_log -- /perception/debug /log_root/1742772196122282243
/perception/debug: 1742772332091220010
{
  "time_of_validity": 1742772332091200120,
  "video_tov": 1742772331892778617,
  "lidar_tov": 1742772331890505331,
  "pose_tov": 1742772331887501886,
  "tov_spread": 5276731
}

/perception/debug: 1742772332291464399
{
  "time_of_validity": 1742772332291430489,
  "video_tov": 1742772332092797397,
  "lidar_tov": 1742772332090508940,
  "pose_tov": 1742772332087513466,
  "tov_spread": 5283931
}
...
```

## GPS

There are two cogs defined in `gps/gps.clk`, a GpsDriverCog and a Gps DeviceCog.

An actual GPS device would send the GPS packets it receives over a transport such as UDP.
The GpsDeviceCog simulates a GPS device that publishes messages received from a simulated GPS device.
The GpsDeviceCog definition declares a single output for the raw GPS messages and declares that the cog runs every 100 milliseconds.
The GpsDeviceCog implementation just sets the location to a fixed position.

```clk
// Cog to simulate the GPS device sending raw GPS packets over UDP
cog GpsDeviceCog
{
  outputs
  {
    raw_gps: Tappy<gps_message::GpsMessage>;
  }

  execution
  {
    condition periodic_100ms: time_since_last_exec(100ms);
    execute when: periodic_100ms;
  }
}
```

The GpsDriverCog simulates a driver that translates the raw GPS messages into the format used by the rest of the system.
For the demo the GpsDriverCog just republishes the raw GPS packet on a different channel.
The `raw_gps` input condition in the GpsDriverCog specifies `min=1` and `max=1` so that it receives a callback for every message published by the GpsDeviceCog.

```clk
// Cog to simulate a driver that converts raw GPS packets into GPS messages
cog GpsDriverCog
{
  inputs
  {
    raw_gps: Tappy<gps_message::GpsMessage>;
  }

  outputs
  {
    gps: Tappy<gps_message::GpsMessage>;
  }

  execution
  {
    condition new_raw_gps: new_message(raw_gps, max=1);
    execute when: new_raw_gps;
  }
}
```

## Logged metadata validation test

The demo system has a `validate_logged_channel_metadata` test that checks that schema changes for logged channels don't break backward compability against the schema stored in `resources/demo_system.demo_system_sys.logged_channel_metadata.pbbin`.
This test will fail if a change to any logged schema breaks backward compability with the previous schema.

## IMU

The demo IMU driver defined in `imu/imu.clk` mirrors the code in the gps directory.
Two IMU cogs are defined, ImuDeviceCog and ImuDriverCog.
The ImuDeviceCog runs every 10 milliseconds and generates some simulated messages with contrived accelerations to get the vehicle position to change slightly over time without going anywhere.
The ImuDriverCog republishes the raw IMU packets on a different channel.

The channels and cogs used in the driver cog are parameterized as an example of how parameterized cogs and channels work in Clockwork.

```clk
// Cog to simulate the IMU device sending raw IMU packets over UDP
cog ImuDeviceCog
{
  parameters
  {
    msg_type: Type;
  }

  outputs
  {
    raw_imu: Tappy<MsgType>;
  }

  execution
  {
    condition periodic_10ms: time_since_last_exec(10ms);
    execute when: periodic_10ms;
  }
}

instantiate ImuDeviceCog<msg_type=imu_message::ImuMessage>;

// Cog to simulate a driver that converts raw IMU packets into IMU messages
cog ImuDriverCog
{
  parameters
  {
    msg_type: Type;
  }

  inputs
  {
    raw_imu: Tappy<imu_message::ImuMessage>;
  }

  outputs
  {
    imu: Tappy<imu_message::ImuMessage>;
  }

  execution
  {
    condition new_raw_imu: new_message(raw_imu, max=1);
    execute when: new_raw_imu;
  }
}

instantiate ImuDriverCog<msg_type=imu_message::ImuMessage>;
```

## Localization

Localization is implemented in three cogs: LocalizationInitCog, LocalizationGpsCog and LocalizationImuCog.
The localization cogs consume messages from the IMU and GPS drivers and do some localization like stuff to publish a simulated pose message that contains the vehicle position at a given time.

Localization maintains the estimated position and estimated velocity in state defined in a Clockwork schema that gets updated each time a GPS or IMU message is received.
The sole purpose of the LocalizationInitCog is to initialize the localization state at startup.
An init cog is not really necessary for this example because the default constructor does the same thing, and if you want to let the default constructor handle initialization then an init Cog is not needed.

```clk
// Initialize the localization state
cog LocalizationInitCog
{
  states
  {
    state: Tappy<localization_state::LocalizationState>
    {
      mutable: true;
    }
  }

  execution
  {
    execute when: init;
  }
}
```

The LocalizationGpsCog updates the estimated position and velocity stored in the localization state each GPS message by averaging the values in the state with the values received from GPS.
The GPS velocity is calculated from the current and previous GPS positions for demo purposes.
By default, the input view only contains the most recent message received on the channel.
Setting `max_msgs: 2` on the gps input view means that we will have access to the current and previous message once two messages are in the buffer.
See [Clockwork channels, input views, and conditions](../../docs/concepts/input_views_triggers.md) for more in input views and triggers.
The Cog implementation to handle the case where only one message has been received.

```clk
// Update pose from GPS messages
cog LocalizationGpsCog
{
  states
  {
    state: Tappy<localization_state::LocalizationState>
    {
      mutable: true;
    }
  }

  inputs
  {
    gps: Tappy<gps_message::GpsMessage>
    {
      max_msgs: 2;
    }
  }

  execution
  {
    condition new_gps: new_message(gps, max=1);
    execute when: new_gps;
  }
}

```

The LocalizationImuCog updates the estimated velocity using the accelerations from the imu messages and then uses the updated velocity to update the estimated location before publishing a pose message with the current estimated position and velocity.
Updates from the IMU use the previous two IMU messages so this Cog also sets `max_msgs: 2` on the input.

```clk
// Update pose from IMU messages and publish a pose message
cog LocalizationImuCog
{
  states
  {
    state: Tappy<localization_state::LocalizationState>
    {
      mutable: true;
    }
  }

  inputs
  {
    imu: Tappy<imu_message::ImuMessage>
    {
      max_msgs: 2;
    }
  }

  outputs
  {
    pose: Tappy<pose_message::PoseMessage>;
  }

  execution
  {
    condition new_imu: new_message(imu, max=1);

    execute when: new_imu;
  }
}
```

## Lidar

Lidar is implemented in two Cogs: a driver Cog to publish raw messages and a driver Cog to turn the raw messages into point clouds.

The LidarDeviceCog simulates the lidar device by publishing raw lidar messages at 100hz.
Each raw lidar message contains simulated lidar returns for 18 degrees of azimuth, so that 20 raw messages make up a full 360 degree sweep.
The LidarDeviceCog stores an index in the state that ranges from 0 to 19 to compute the starting azimuth for each raw message.
An initialization Cog is not needed for the lidar state because the default constructor sets the sweep index to zero.

```clk
// Cog to simulate the lidar device sending raw lidar packets
cog LidarDeviceCog
{
  states
  {
    state: Tappy<lidar_state::LidarState>
    {
      mutable: true;
    }
  }

  outputs
  {
    raw_lidar: Tappy<lidar_message::RawLidarMessage<lidar_message::returns_per_message, 40>>;
  }

  execution
  {
    condition periodic_10ms: time_since_last_exec(10ms);
    execute when: periodic_10ms;
  }
}
```

The LidarDriverCog processes batches of 20 raw lidar messages to produce a motion compensated point cloud containing all of the points for the sweep.
In order to receive the last 25 pose messages, the pose input sets `max_msgs: 20` on the input view then `min=25, max=25` is set on the pose_history input condition.
The last 25 pose messages are used to extrapolate the vehicle position for the times in the sweep.
In order to receive the last 25 pose messages, the pose input sets `max_msgs: 20` on then `min=25, max=25` is set on the pose_history input condition.
The LidarDriverCog also needs a memory resource because our simulated pose filter uses the resource to allocate the map used to extrapolate poses from timestamps.

```clk
// Cog to convert raw lidar packets into motion compensated point clouds
cog LidarDriverCog
{
  resources
  {
    memory: persistent;
  }

  inputs
  {
    raw_lidar: Tappy<lidar_message::RawLidarMessage<lidar_message::returns_per_message, 40>>
    {
      max_msgs: 20;
    }
    pose: Tappy<pose_message::PoseMessage>
    {
      max_msgs: 25;
    }
  }

  outputs
  {
    lidar_sweep: Tappy<lidar_message::LidarSweepMessage<lidar_message::points_per_sweep>>;
  }

  execution
  {
    condition new_raw_lidar: new_message(raw_lidar, min=20, max=20);
    condition pose_history: any_message(pose, min=25, max=25);
    execute when: new_raw_lidar and pose_history;
  }
}
```

## Camera

The camera Cogs are implemented in two Cogs.
The CameraDeviceCog runs at 10hz and sends simulated raw images containing white noise in YUV411 video format.

```clk
// Cog to simulate receiving YUV411 video frames from the camera
cog CameraDeviceCog
{
  states
  {
    state: CameraState
    {
      mutable: true;
    }
  }

  outputs
  {
    raw_camera: Tappy<video_message::Yuv411Image<video_message::raw_image_size_bytes>>;
  }

  execution
  {
    condition periodic_100ms: time_since_last_exec(100ms);
    execute when: periodic_100ms;
  }
}
```

The CameraDriverCog converts the raw images to YUV444 video format.

```clk
// Cog to convert the raw YUV411 frames to YUV444 for processing
cog CameraDriverCog
{
  inputs
  {
    raw_camera: Tappy<video_message::Yuv411Image<video_message::raw_image_size_bytes>>;
  }

  outputs
  {
    camera: Tappy<video_message::Yuv444Image<video_message::image_size_bytes>>;
  }

  execution
  {
    condition new_raw_camera: new_message(raw_camera, max=1);
    execute when: new_raw_camera;
  }
}
```

## Perception

The perception Cogs don't try to perceive anything, but rather serve as an example of using an `ApproxAligner` to collect a set of inputs approximately aligned at the same time of validity.
See [Stream Aligners](../../docs/reference/stream_aligners.md) for more on stream aligners.

The PerceptionInitCog reads the aligner configuration from a text protobuf and uses it to initialize the aligner state.
The PerceptionInitCog also publishes the aligner configuration to a persistent channel.
Logging to a persistent channel ensures that any slice of the log will have the most recent message from that channel at the start of the log.

```clk
cog PerceptionInitCog
{
  configs
  {
    aligner_config: Tappy<approx_aligner_config::ApproxAlignerConfig>;
  }

  outputs
  {
    aligner_config_out: Tappy<approx_aligner_config::ApproxAlignerConfig>;
  }

  execution
  {
    execute when: init;
  }
}
```

The PerceptionCog inputs are a one second history from each of the input channels: camera, lidar, and pose.
The PerceptionCog runs whenever a new lidar or camera message is received and uses the approx aligner to find when the times of validity from the three input channels line up and outputs a debug message that says how well the aligner did.

```clk
// Perception cog uses an aligner to collect messages from the same time
cog PerceptionCog
{
  resources
  {
    memory: persistent;
  }

  configs
  {
    aligner_config: Tappy<approx_aligner_config::ApproxAlignerConfig>;
  }

  states
  {
    aligner_state: Tappy<approx_aligner_config::ApproxAlignerState>
    {
        mutable: true;
    }
  }

  inputs
  {
    pose: Tappy<pose_message::PoseMessage>
    {
      max_msgs: 100;
      manual_cursor: true;
    }

    video: Tappy<video_message::Yuv444Image<video_message::image_size_bytes>>
    {
      max_msgs: 10;
      manual_cursor: true;
    }

    lidar: Tappy<lidar_message::LidarSweepMessage<lidar_message::points_per_sweep>>
    {
      max_msgs: 10;
      manual_cursor: true;
    }
  }

  outputs
  {
    debug: Tappy<perception_message::PerceptionDebugMessage>;
  }

  execution
  {
    condition new_video: new_message(input=video);
    condition new_lidar: new_message(input=lidar);
    execute when: new_video or new_lidar;
  }
}
```

The appox aligner configuration file is used to tune the aligner.
In this case, we want the last (or most recent) messages from the inputs.
The minimum score threshold indicates how well we want the messages to align, higher values mean better alignment.
The minimum and maximum wait times tell the aligner how long to wait when looking for alignment.
Higher wait times increase the quality of the alignment but may also increase latency.
It is worth experimenting with the configuration settings to find the values that produce the desired results.

```textproto
find_type: last
minimum_score_threshold: 1.5
minimum_wait_time {
  seconds: 0
  nanos: 90000000
}
maximum_wait_time: 190000000
```
