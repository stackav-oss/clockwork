# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for graphir."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.composition import graphir
from clockwork.dsl.ir import box, cog, compiler, pubsub, udp
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_hellomod(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")), fs_importer
    )
    box_template_ir = module.inner_scope.lookup("HelloBox", recursive=False)
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    hello_chan = graphir.lookup_channel("HelloChan", module.context)
    hello_chan_ir = pubsub.lookup_channel("HelloChan", module.context)
    assert hello_chan.ir_node is hello_chan_ir
    multi_chan = graphir.lookup_channel("many_publishers", module.context)
    conns = [graphir.from_ir_connection(conn, module.context) for conn in box_ir.connections]
    assert len(conns) == 18
    (
        mem_hello_conn,
        hello_config,
        _ro_hello,
        _ro_hello_to_rw_hello_conn,
        _rw_hello_conn,
        _extern_hello_conn,
        latest_in,
        history_in,
        multi_in,
        out_world,
        out_goodbye,
        out_multi1,
        out_multi2,
        _diagnostics_conn,
        in_udp,
        out_udp,
        _ro_hello_init_conn,
        _rw_hello_init_conn,
    ) = conns
    assert isinstance(mem_hello_conn, graphir.MemoryResourceConnection)
    assert mem_hello_conn.memory_resource.resource_type == box.MemResourceType.HEAP
    assert mem_hello_conn.memory_resource.max_size == 1e6
    assert isinstance(hello_config, graphir.ConfigConnection)
    assert isinstance(hello_config.config_instance, box.SerializedDataFileInstance)
    assert hello_config.config_instance.file_path == Path("foo/bar.txtpb")
    assert isinstance(latest_in, graphir.ChannelToCogSubscribeConnection)
    assert isinstance(history_in, graphir.ChannelToCogSubscribeConnection)
    assert isinstance(multi_in, graphir.ChannelToCogSubscribeConnection)
    assert isinstance(out_world, graphir.ChannelToCogPublishConnection)
    assert isinstance(out_goodbye, graphir.ChannelToCogPublishConnection)
    assert isinstance(out_multi1, graphir.ChannelToCogPublishConnection)
    assert isinstance(out_multi2, graphir.ChannelToCogPublishConnection)
    assert all(conn.channel is hello_chan for conn in (latest_in, history_in, out_world))
    assert out_goodbye.channel is graphir.lookup_channel(
        "Name that doesn't follow reasonable conventions!", module.context
    )
    assert all(conn.channel is multi_chan for conn in (multi_in, out_multi1, out_multi2))
    latest_in_instance = latest_in.cog_instance_member
    assert isinstance(latest_in_instance, cog.CogInstanceMember)
    for conn in (history_in, out_world, out_goodbye):
        assert isinstance(conn, graphir.ChannelToCogSubscribeConnection | graphir.ChannelToCogPublishConnection)
        assert latest_in_instance.cog_instance is conn.cog_instance_member.cog_instance
    multi_in_instance = multi_in.cog_instance_member
    for conn2 in (multi_in, out_multi1, out_multi2):
        assert isinstance(conn2, graphir.ChannelToCogSubscribeConnection | graphir.ChannelToCogPublishConnection)
        assert multi_in_instance.cog_instance is conn2.cog_instance_member.cog_instance

    incoming_udp_socket = module.inner_scope.lookup("IncomingUdpSocket", recursive=False)
    assert isinstance(incoming_udp_socket, udp.UdpSocket)
    assert isinstance(in_udp, graphir.ChannelToUdpPublishConnection)
    assert isinstance(in_udp.socket_endpoint, udp.UdpSocketEndpointInstance)
    assert in_udp.socket_endpoint.socket is incoming_udp_socket
    assert in_udp.socket_endpoint.endpoint is incoming_udp_socket.producer_endpoint

    outgoing_udp_socket = module.inner_scope.lookup("OutgoingUdpSocket", recursive=False)
    assert isinstance(outgoing_udp_socket, udp.UdpSocket)
    assert isinstance(out_udp, graphir.ChannelToUdpSubscribeConnection)
    assert isinstance(out_udp.socket_endpoint, udp.UdpSocketEndpointInstance)
    assert out_udp.socket_endpoint.socket is outgoing_udp_socket
    assert out_udp.socket_endpoint.endpoint is outgoing_udp_socket.observer_endpoint


def test_message_type_mismatch(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// Channel
channel Chan
{
    message_type: Tachyon<hellomsg::GenericMsg<Float32, 32>>;
    max_num_messages: 10;
}

box TestBox
{
    new hello_cog: hellocog::HelloCog;
    connect Chan to hello_cog.latest_hello;
    connect Chan to hello_cog.history_of_hellos;
    connect hello_cog.out_world to Chan;
    connect hello_cog.out_goodbye to Chan;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)
    box_template_ir = module.inner_scope.lookup("TestBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    with pytest.raises(
        TypeError,
        match=re.escape(
            "Attempt to connect channel type ::Tachyon<schema=@clockwork::clockwork::dsl::tests::support::hellomsg::GenericMsg<data_type=::Float32,data_size=32>> to input type ::Tachyon<schema=@clockwork::clockwork::dsl::tests::support::hellomsg::HelloMsg>"
        ),
    ):
        graphir.from_ir_connection(box_ir.connections[0], module.context)


def test_udp_invalid_message_type(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::hellomsg;
use clockwork::io::var_packet;
// Doc
channel WrongMessageType
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}
// Doc
udp_socket Socket
{
    address: 127.0.0.1;
    port: 54321;
    direction: incoming;
    message_type: Tachyon<var_packet::VarPacket<4>>;
}

box TestBox
{
    new socket: Socket;
    connect socket to WrongMessageType;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)

    box_template_ir = module.inner_scope.lookup("TestBox")

    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    assert len(box_ir.connections) == 1
    with pytest.raises(
        TypeError,
        match=r"Mismatched 'message_type' between socket 'Socket' and channel 'WrongMessageType'.",
    ):
        graphir.from_ir_connection(box_ir.connections[0], module.context)


def test_udp_invalid_message_size(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::io::var_packet;
// Doc
channel WrongMessageSize
{
    message_type: Tachyon<var_packet::VarPacket<4>>;
    max_num_messages: 10;
}
// Doc
udp_socket SocketWrongBytes
{
    address: 127.0.0.1;
    port: 4321;
    direction: incoming;
    message_type: Tachyon<var_packet::VarPacket<5>>;
}

box BoxWrongSize
{
    new socket: SocketWrongBytes;
    connect socket to WrongMessageSize;
}

"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test"), fs_importer)

    box_template_ir = module.inner_scope.lookup("BoxWrongSize")

    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    assert len(box_ir.connections) == 1
    with pytest.raises(
        TypeError,
        match=r"Mismatched 'message_type' between socket 'SocketWrongBytes' and channel 'WrongMessageSize'.",
    ):
        graphir.from_ir_connection(box_ir.connections[0], module.context)


def test_data_source_connections(fs_importer: FilesystemImporter) -> None:
    source_text = """
use clockwork::dsl::tests::support::hellocog;
use clockwork::dsl::tests::support::hellomsg;

// TestChan
channel TestChan
{
    message_type: Tachyon<hellomsg::HelloMsg>;
    max_num_messages: 10;
}

box TestBox
{
    new first_msg: FirstMessage(channel=TestChan);
    new fallback_file: SerializedDataFile(representation=Protobuf<hellomsg::HelloMsg>, path="fallback.textproto");
    connect fallback_file to first_msg.fallback;
    new state: State(representation=Tachyon<hellomsg::HelloMsg>);
    connect first_msg to state;
}
"""
    module = compiler.compile_source_text(source_text, ModuleID(CLK_REPO, "test_data_source_connections"), fs_importer)
    box_template_ir = module.inner_scope.lookup("TestBox")
    assert isinstance(box_template_ir, box.BoxTemplate)
    box_ir = box_template_ir.make_instance(cst_node=None, module=module, scope=module.inner_scope, name="box", doc=None)
    # Find the relevant connections
    fallback_conn = None
    init_conn = None
    for conn in box_ir.connections:
        gconn = graphir.from_ir_connection(conn, module.context)
        if isinstance(gconn, graphir.DataSourceFallbackConnection):
            fallback_conn = gconn
        if isinstance(gconn, graphir.InitDataSourceConnection):
            init_conn = gconn
    assert fallback_conn is not None, "DataSourceFallbackConnection not found"
    assert init_conn is not None, "InitDataSourceConnection not found"
    # Check types and fields
    assert isinstance(fallback_conn.data_source, box.FirstMessageInstance)
    assert isinstance(fallback_conn.fallback_data_source, box.SerializedDataFileInstance)
    assert isinstance(init_conn.data_source, box.FirstMessageInstance)
    assert isinstance(init_conn.target_instance, box.StateInstance)
    # Fallback should match the fallback_file instance
    assert fallback_conn.fallback_data_source.file_path == Path("fallback.textproto")
    test_chan = graphir.lookup_channel("TestChan", module.context).ir_node
    assert fallback_conn.data_source.channel is test_chan
    assert init_conn.data_source.channel is test_chan
