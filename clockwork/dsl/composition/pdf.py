# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Python interface to Process Description Files (PDF).

Note: You only need to import this module if you need to *instantiate* these
schemas.  If you only use schema instances or need schema type annotations,
import pdfproto instead.
"""

from __future__ import annotations

from pathlib import Path
from typing import TYPE_CHECKING, Final

from clockwork.dsl.ir import compiler
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.serialization.py import tachyon_dyn

if TYPE_CHECKING:
    from clockwork.dsl.composition import pdfproto

PD_MODULE: Final = compiler.compile_source_file(
    ModuleID.from_path(CLK_REPO, Path("clockwork/common/process_description.clk")),
    FilesystemImporter(compile_fn=compiler.compile_source_file),
)


MemoryResourceType: Final[type[pdfproto.MemoryResourceTypeEnum]] = tachyon_dyn.get_enum(
    PD_MODULE.context, PD_MODULE, "MemoryResourceType"
)[0]

NotConnectedEndpointType: Final[type[pdfproto.NotConnectedEndpointTypeEnum]] = tachyon_dyn.get_enum(
    PD_MODULE.context, PD_MODULE, "NotConnectedEndpointType"
)[0]

ProcessDescription: Final[type[pdfproto.ProcessDescription]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context,
    PD_MODULE,
    "ProcessDescription",
    max_instance_path_size=512,
    max_cog_instances=501,
    max_endpoints_per_cog=55,
    max_state_instances=267,
    max_state_connections=509,
    max_config_instances=234,
    max_config_connections=304,
    max_file_path_size=4096,
    max_init_cogs=202,
    max_publishers=2048,
    max_pubsub_connections=920,
    max_memory_resources=504,
    max_memory_resource_connections=488,
    max_timers=45,
    max_io_connections=32,
    max_channel_name_len=300,
    max_not_connected_endpoints=1024,
)[0]


PubSubGraph: Final[type[pdfproto.PubSubGraph]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context,
    PD_MODULE,
    "PubSubGraph",
    max_publishers=2048,
    max_pubsub_connections=920,
    max_channel_name_len=300,
)[0]


CogInstanceDescription: Final[type[pdfproto.CogInstanceDescription]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context, PD_MODULE, "CogInstanceDescription", max_endpoints_per_cog=55, max_instance_path_size=512
)[0]

EndpointInstanceDescription: Final[type[pdfproto.EndpointInstanceDescription]] = tachyon_dyn.get_schema_dataclass(
    PD_MODULE.context, PD_MODULE, "EndpointInstanceDescription"
)[0]

PublishEndpoint: Final[type[pdfproto.PublishEndpoint]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context, PD_MODULE, "PublishEndpoint", max_channel_name_len=300
)[0]

PinionBufferLayout: Final[type[pdfproto.PinionBufferLayout]] = tachyon_dyn.get_schema_dataclass(
    PD_MODULE.context, PD_MODULE, "PinionBufferLayout"
)[0]

PubSubConnection: Final[type[pdfproto.PubSubConnection]] = tachyon_dyn.get_schema_dataclass(
    PD_MODULE.context, PD_MODULE, "PubSubConnection"
)[0]

StateGraph: Final[type[pdfproto.StateGraph]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context,
    PD_MODULE,
    "StateGraph",
    max_state_instances=267,
    max_instance_path_size=512,
    max_state_connections=509,
)[0]

StateInstanceDescription: Final[type[pdfproto.StateInstanceDescription]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context, PD_MODULE, "StateInstanceDescription", max_instance_path_size=512
)[0]

StateConnection: Final[type[pdfproto.StateConnection]] = tachyon_dyn.get_schema_dataclass(
    PD_MODULE.context, PD_MODULE, "StateConnection"
)[0]

ConfigGraph: Final[type[pdfproto.ConfigGraph]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context,
    PD_MODULE,
    "ConfigGraph",
    max_instance_path_size=512,
    max_config_instances=234,
    max_config_connections=304,
    max_file_path_size=4096,
)[0]

ConfigInstanceDescription: Final[type[pdfproto.ConfigInstanceDescription]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context, PD_MODULE, "ConfigInstanceDescription", max_instance_path_size=512, max_file_path_size=4096
)[0]

ConfigConnection: Final[type[pdfproto.ConfigConnection]] = tachyon_dyn.get_schema_dataclass(
    PD_MODULE.context, PD_MODULE, "ConfigConnection"
)[0]

MemoryResource: Final[type[pdfproto.MemoryResource]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context, PD_MODULE, "MemoryResource", max_instance_path_size=512
)[0]

MemoryResourceConnection: Final[type[pdfproto.MemoryResourceConnection]] = tachyon_dyn.get_schema_dataclass(
    PD_MODULE.context, PD_MODULE, "MemoryResourceConnection"
)[0]

MemoryResourceGraph: Final[type[pdfproto.MemoryResourceGraph]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context,
    PD_MODULE,
    "MemoryResourceGraph",
    max_instance_path_size=512,
    max_memory_resources=504,
    max_memory_resource_connections=488,
)[0]

TimerInstanceDescription: Final[type[pdfproto.TimerInstanceDescription]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context, PD_MODULE, "TimerInstanceDescription", max_instance_path_size=512
)[0]

IoConnectionInstanceDescription: Final[type[pdfproto.IoConnectionInstanceDescription]] = (
    tachyon_dyn.get_instantiation_dataclass(
        PD_MODULE.context, PD_MODULE, "IoConnectionInstanceDescription", max_instance_path_size=512
    )[0]
)

NotConnectedEndpoint: Final[type[pdfproto.NotConnectedEndpoint]] = tachyon_dyn.get_instantiation_dataclass(
    PD_MODULE.context, PD_MODULE, "NotConnectedEndpoint"
)[0]
