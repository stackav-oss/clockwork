# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for data source configurations."""

from __future__ import annotations

import pytest
from clockwork.dsl.composition import genpd, pdf, system
from clockwork.dsl.composition.gen_logger_configs import gen_channel_publisher_configs
from clockwork.dsl.ir import box, compiler, system_target
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID
from clockwork.dsl.serialization import tachyon_reg


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_data_source_file_to_config(fs_importer: FilesystemImporter) -> None:
    """Test SerializedDataFileInstance connected as data source to config."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

// Test cog
cog TestCog
{
    configs
    {
        cfg: Tappy<hellomsg::HelloMsg>;
    }

    execution
    {
        condition periodic: time_since_last_exec(1ms);
        execute when: periodic;
    }
}

box TestBox
{
    new hello_cog: TestCog;
    new config_file: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="config.textproto");
    connect config_file to hello_cog.cfg;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_data_source_file_config"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.config_graph.config_instances) == 1
    (config_instance,) = pd.config_graph.config_instances
    assert config_instance.init_data_source != pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL

    data_source_idx = config_instance.init_data_source
    assert len(pd.data_sources) > data_source_idx
    data_source = pd.data_sources[data_source_idx]
    assert data_source.data_source_type == pdf.DataSourceType.file
    assert data_source.source_path_or_name == "config.textproto"
    assert data_source.fallback_source == pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL


def test_data_source_first_message_to_state(fs_importer: FilesystemImporter) -> None:
    """Test FirstMessageInstance connected as data source to state."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

// Channel
channel TestChan
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new first_msg: FirstMessage(channel=TestChan);
    new state: State(representation=Tachyon<hellomsg::HelloMsg>);
    connect first_msg to state;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(
        source_text, ModuleID(CLK_REPO, "test_data_source_first_msg_state"), fs_importer
    )
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.state_graph.state_instances) == 1
    (state_instance,) = pd.state_graph.state_instances
    assert state_instance.init_data_source != pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL

    data_source_idx = state_instance.init_data_source
    assert len(pd.data_sources) > data_source_idx
    data_source = pd.data_sources[data_source_idx]
    assert data_source.data_source_type == pdf.DataSourceType.log_first_message
    assert data_source.source_path_or_name == "TestChan"
    assert data_source.fallback_source == pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL


def test_data_source_fallback_chain(fs_importer: FilesystemImporter) -> None:
    """Test fallback chain: FirstMessage -> SerializedDataFile."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

// Channel
channel TestChan
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new first_msg: FirstMessage(channel=TestChan, allow_default=false);
    new fallback_file: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="fallback.textproto");
    connect fallback_file to first_msg.fallback;
    new state: State(representation=Tachyon<hellomsg::HelloMsg>);
    connect first_msg to state;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_data_source_fallback"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.state_graph.state_instances) == 1
    (state_instance,) = pd.state_graph.state_instances
    assert state_instance.init_data_source != pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL

    # First data source (first_msg)
    primary_idx = state_instance.init_data_source
    primary_ds = pd.data_sources[primary_idx]
    assert primary_ds.data_source_type == pdf.DataSourceType.log_first_message
    assert primary_ds.source_path_or_name == "TestChan"
    assert primary_ds.fallback_source != pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL
    assert primary_ds.fallback_source != pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL

    # Fallback data source (fallback_file)
    fallback_idx = primary_ds.fallback_source
    fallback_ds = pd.data_sources[fallback_idx]
    assert fallback_ds.data_source_type == pdf.DataSourceType.file
    assert fallback_ds.source_path_or_name == "fallback.textproto"
    assert fallback_ds.fallback_source == pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL


def test_data_source_multiple_configs_and_states(fs_importer: FilesystemImporter) -> None:
    """Test multiple data sources for different configs and states."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

// Test cog
cog TestCog
{
    configs
    {
        cfg: Tappy<hellomsg::HelloMsg>;
    }

    execution
    {
        condition periodic: time_since_last_exec(1ms);
        execute when: periodic;
    }
}

// Channel 1
channel Chan1
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

// Channel 2
channel Chan2
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new cog1: TestCog;
    new cog2: TestCog;

    new first_msg1: FirstMessage(channel=Chan1);
    new first_msg2: FirstMessage(channel=Chan2);
    new file1: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="config1.textproto");
    new file2: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="config2.textproto");

    new state1: State(representation=Tachyon<hellomsg::HelloMsg>);
    new state2: State(representation=Tachyon<hellomsg::HelloMsg>);

    connect first_msg1 to state1;
    connect first_msg2 to state2;
    connect file1 to cog1.cfg;
    connect file2 to cog2.cfg;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_multiple_data_sources"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.state_graph.state_instances) == 2
    assert len(pd.config_graph.config_instances) == 2
    assert len(pd.data_sources) == 4  # 2 first messages + 2 files

    for state in pd.state_graph.state_instances:
        if state.instance_path_name == "@clockwork::test_multiple_data_sources.test_system.box.state1":
            ds = pd.data_sources[state.init_data_source]
            assert ds.data_source_type == pdf.DataSourceType.log_first_message
            assert ds.source_path_or_name == "Chan1"
        elif state.instance_path_name == "@clockwork::test_multiple_data_sources.test_system.box.state2":
            ds = pd.data_sources[state.init_data_source]
            assert ds.data_source_type == pdf.DataSourceType.log_first_message
            assert ds.source_path_or_name == "Chan2"
        else:
            pytest.fail(f"Unexpected state instance: {state.instance_path_name}")
    for config in pd.config_graph.config_instances:
        if config.instance_path_name == "@clockwork::test_multiple_data_sources.test_system.box.file1":
            ds = pd.data_sources[config.init_data_source]
            assert ds.data_source_type == pdf.DataSourceType.file
            assert ds.source_path_or_name == "config1.textproto"
        elif config.instance_path_name == "@clockwork::test_multiple_data_sources.test_system.box.file2":
            ds = pd.data_sources[config.init_data_source]
            assert ds.data_source_type == pdf.DataSourceType.file
            assert ds.source_path_or_name == "config2.textproto"
        else:
            pytest.fail(f"Unexpected config instance: {config.instance_path_name}")


def test_data_source_shared_fallback(fs_importer: FilesystemImporter) -> None:
    """Test multiple data sources sharing the same fallback."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

// Channel 1
channel Chan1
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

// Channel 2
channel Chan2
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new first_msg1: FirstMessage(channel=Chan1, allow_default=false);
    new first_msg2: FirstMessage(channel=Chan2, allow_default=false);
    new shared_fallback: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="shared.textproto");

    connect shared_fallback to first_msg1.fallback;
    connect shared_fallback to first_msg2.fallback;

    new state1: State(representation=Tachyon<hellomsg::HelloMsg>);
    new state2: State(representation=Tachyon<hellomsg::HelloMsg>);

    connect first_msg1 to state1;
    connect first_msg2 to state2;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_shared_fallback"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.data_sources) == 3  # 2 first messages + 1 shared file

    # Find the file data source
    file_ds = next(ds for ds in pd.data_sources if ds.data_source_type == pdf.DataSourceType.file)
    assert file_ds.source_path_or_name == "shared.textproto"

    # Both first message data sources should fallback to the same file
    first_msg_dss = [ds for ds in pd.data_sources if ds.data_source_type == pdf.DataSourceType.log_first_message]
    assert len(first_msg_dss) == 2
    for ds in first_msg_dss:
        assert ds.fallback_source == pd.data_sources.index(file_ds)


def test_data_source_no_fallback(fs_importer: FilesystemImporter) -> None:
    """Test data source with no fallback specified."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

// Channel
channel TestChan
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new first_msg: FirstMessage(channel=TestChan, allow_default=false);
    new state: State(representation=Tachyon<hellomsg::HelloMsg>);
    connect first_msg to state;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_no_fallback"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.state_graph.state_instances) == 1
    (state_instance,) = pd.state_graph.state_instances
    data_source_idx = state_instance.init_data_source
    data_source = pd.data_sources[data_source_idx]
    assert data_source.fallback_source == pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL


def test_data_source_file_to_state(fs_importer: FilesystemImporter) -> None:
    """Test SerializedDataFileInstance connected as data source to state."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

box TestBox
{
    new state_file: SerializedDataFile(representation=Tachyon<hellomsg::HelloMsg>, path="state.textproto");
    new state: State(representation=Tachyon<hellomsg::HelloMsg>);
    connect state_file to state;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_data_source_file_state"), fs_importer)
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.state_graph.state_instances) == 1
    (state_instance,) = pd.state_graph.state_instances
    assert state_instance.init_data_source != pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL

    data_source_idx = state_instance.init_data_source
    assert len(pd.data_sources) > data_source_idx
    data_source = pd.data_sources[data_source_idx]
    assert data_source.data_source_type == pdf.DataSourceType.file
    assert data_source.source_path_or_name == "state.textproto"
    assert data_source.fallback_source == pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL


def test_data_source_first_message_to_config(fs_importer: FilesystemImporter) -> None:
    """Test FirstMessageInstance connected as data source to config."""
    source_text = """
use clockwork::dsl::tests::support::hellomsg;

// Test cog
cog TestCog
{
    configs
    {
        cfg: Tappy<hellomsg::HelloMsg>;
    }

    execution
    {
        condition periodic: time_since_last_exec(1ms);
        execute when: periodic;
    }
}

// Channel
channel TestChan
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new hello_cog: TestCog;
    new first_msg: FirstMessage(channel=TestChan);
    connect first_msg to hello_cog.cfg;
}

cpu_domain TestCpu;

box TestSys
{
    new box: TestBox;
    new proc: Process(executable=exe);
    apply HostProcess(process=proc) in box;
    apply HostCpuDomain(cpu_domain=TestCpu) to proc;
}

cpp_executable exe
{
    casing
    {
    }
}

system_target test_system
{
    box: TestSys;
}
"""
    module = compiler.compile_source_text(
        source_text, ModuleID(CLK_REPO, "test_data_source_first_msg_config"), fs_importer
    )
    sys_ir = module.inner_scope.lookup("test_system", recursive=False)
    assert isinstance(sys_ir, system_target.UnresolvedSystemTarget)

    logical_system = system.make_system(
        [sys_ir.get_resolved().box_instance], sys_ir.module, sys_ir.require_logging_policies
    )
    physical_system = system.make_physical_system(logical_system)
    process_descs = genpd.gen_pd_sys(physical_system)
    (pd,) = process_descs.values()

    assert len(pd.config_graph.config_instances) == 1
    (config_instance,) = pd.config_graph.config_instances
    assert config_instance.init_data_source != pdf.DEFAULT_CONSTRUCT_DATA_SOURCE_SENTINEL

    data_source_idx = config_instance.init_data_source
    assert len(pd.data_sources) > data_source_idx
    data_source = pd.data_sources[data_source_idx]
    assert data_source.data_source_type == pdf.DataSourceType.log_first_message
    assert data_source.source_path_or_name == "TestChan"
    assert data_source.fallback_source == pdf.NO_FALLBACK_DATA_SOURCE_SENTINEL

    # Verify channel publisher config
    publisher_configs = gen_channel_publisher_configs(physical_system)
    assert len(publisher_configs) == 1
    (publisher_config,) = publisher_configs.values()
    assert len(publisher_config.channels) == 1
    channel_config = publisher_config.channels[0]
    assert channel_config.channel_name == "TestChan"
    assert channel_config.message_size > 0, "FirstMessage channel should have non-zero message_size"
    # The message_size should match the Tachyon constraint size for HelloMsg
    # Get the schema from the FirstMessage data source
    first_msg_ds = next(ds for ds in logical_system.data_sources.values() if isinstance(ds, box.FirstMessageInstance))
    assert first_msg_ds.channel.message_repr is not None
    hello_msg_schema = first_msg_ds.channel.message_repr.get_schema()
    constraint = tachyon_reg.constraint_for_type(module.context, hello_msg_schema)
    assert constraint is not None
    assert channel_config.message_size == constraint.size
