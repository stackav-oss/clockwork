# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Test the udp IR node."""

from textwrap import dedent

import pytest
from clockwork.dsl.ir import (
    clkbuiltins,
    compiler,
    importer,
    primitive,
    typesys,
    udp,
)
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


def test_udp_socket() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        udp_socket UDP1
        {
          address: 127.0.0.1;
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }
        """,
    )
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "udp_1"), importer=fs_importer)

    udp_1 = module.inner_scope.lookup("UDP1")
    assert isinstance(udp_1, udp.UdpSocket)
    assert udp_1.address.value == "127.0.0.1"
    assert primitive.decimal_to_int(udp_1.port) == 12345
    assert isinstance(udp_1.message_type, typesys.Instantiation)
    assert udp_1.message_type.instantiates == clkbuiltins.TACHYON

    instance = udp_1.make_instance(
        cst_node=None, module=module, scope=module.inner_scope, name="udp1_instance", doc=None
    )
    assert instance.socket is udp_1
    assert instance.inner_scope.parent is udp_1.inner_scope


def test_udp_socket_invalid_address() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        udp_socket UDP1
        {
          address: 999.0.0.1;
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }
        """,
    )
    with pytest.raises(ValueError, match=r"Octet in IPv4 address must be in range \[0, 255\].  Received: 999.0.0.1"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "invalid_address"), importer=fs_importer)

    source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        multicast_udp_socket BadMulticastGroup
        {
          group_address: 999.0.0.1;
          interface_address: 127.0.0.1;
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }
        """,
    )
    with pytest.raises(ValueError, match=r"Octet in IPv4 address must be in range \[0, 255\].  Received: 999.0.0.1"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "invalid_address"), importer=fs_importer)


def test_udp_socket_invalid_use_of_remote() -> None:
    # Unidirectional socket (IncomingUdp, OutgoingUdp) should only specify "host" and "port"
    # Confusing, so make sure exception is raised if violation.
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        udp_socket InvalidRemoteAddress
        {
          address: 127.0.0.1;
          port: 12345;
          direction: outgoing;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          remote_address:127.0.0.2;
        }
        """,
    )
    with pytest.raises(ValueError, match=r"should not specify remote_address"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "invalid_address"), importer=fs_importer)

    source2 = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        udp_socket InvalidRemoteAddress
        {
          address: 127.0.0.1;
          port: 12345;
          direction: outgoing;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          remote_port: 54321;
        }
        """,
    )
    with pytest.raises(ValueError, match=r"should not specify remote_port"):
        compiler.compile_source_text(source2, ModuleID(CLK_REPO, "invalid_port"), importer=fs_importer)


def test_udp_socket_bidirectional() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    missing_remote = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        udp_socket MissingRemote
        {
          address: 127.0.0.1;
          port: 12345;
          direction: bidirectional;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }
        """,
    )
    with pytest.raises(ValueError, match=r"specify remote_address"):
        compiler.compile_source_text(missing_remote, ModuleID(CLK_REPO, "missing_remote"), importer=fs_importer)

    missing_remote_port = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        udp_socket MissingRemotePort
        {
          address: 127.0.0.1;
          port: 12345;
          direction: bidirectional;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          remote_address: 127.0.0.2;
        }
        """,
    )
    with pytest.raises(ValueError, match=r"specify remote_port"):
        compiler.compile_source_text(
            missing_remote_port, ModuleID(CLK_REPO, "missing_remote_port"), importer=fs_importer
        )

    valid_source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        udp_socket UDP1
        {
          address: 127.0.0.1;
          port: 12345;
          direction: bidirectional;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          remote_address: 127.0.0.2;
          remote_port: 54321;
        }
        """,
    )
    module = compiler.compile_source_text(valid_source, ModuleID(CLK_REPO, "valid"), importer=fs_importer)
    udp_1 = module.inner_scope.lookup("UDP1")

    assert isinstance(udp_1, udp.UdpSocket)
    assert udp_1.address.value == "127.0.0.1"
    assert primitive.decimal_to_int(udp_1.port) == 12345
    assert isinstance(udp_1.remote_address, primitive.IPv4Address)
    assert udp_1.remote_address.value == "127.0.0.2"
    assert isinstance(udp_1.remote_port, primitive.DecimalValue)
    assert primitive.decimal_to_int(udp_1.remote_port) == 54321
    assert isinstance(udp_1.message_type, typesys.Instantiation)
    assert udp_1.message_type.instantiates == clkbuiltins.TACHYON

    instance = udp_1.make_instance(
        cst_node=None, module=module, scope=module.inner_scope, name="udp1_instance", doc=None
    )
    assert instance.socket is udp_1
    assert instance.inner_scope.parent is udp_1.inner_scope


def _make_address_fields(socket_type: str, socket_address: str) -> str:
    if socket_type == "udp_socket":
        return f"address: {socket_address};\n"

    return f"""group_address: {socket_address};
    interface_address: 127.0.0.1;
    """


def _test_udp_socket_options(socket_type: str, socket_address: str) -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        f"""
        use clockwork::io::var_packet;
        // Docs
        {socket_type} WithOptions
        {{
          {_make_address_fields(socket_type, socket_address)}
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;

          options
          {{
            reuse_address: true;
            receive_buffer: 4096byte;
            bind_to_interface: true;
          }}
        }}
        """,
    )

    module = compiler.compile_source_text(
        source, ModuleID(CLK_REPO, f"valid_options_{socket_type}"), importer=fs_importer
    )

    socket_ir = module.inner_scope.lookup("WithOptions")
    assert isinstance(socket_ir, udp.UdpSocket)
    assert socket_ir.options is not None

    assert udp.SocketReuseAddress in socket_ir.options.options
    reuse_address = socket_ir.options.options[udp.SocketReuseAddress]
    assert isinstance(reuse_address, udp.SocketReuseAddress)
    assert reuse_address.value == clkbuiltins.TRUE_VALUE

    assert udp.SocketReceiveBuffer in socket_ir.options.options
    receive_buffer = socket_ir.options.options[udp.SocketReceiveBuffer]
    assert isinstance(receive_buffer, udp.SocketReceiveBuffer)
    assert receive_buffer.value.value == 4096
    assert receive_buffer.value.type_info == clkbuiltins.BYTES

    assert udp.SocketBindToDevice in socket_ir.options.options
    bind = socket_ir.options.options[udp.SocketBindToDevice]
    assert isinstance(bind, udp.SocketBindToDevice)
    assert bind.value == clkbuiltins.TRUE_VALUE
    expected_interface = socket_address if socket_type == "udp_socket" else "127.0.0.1"
    assert bind.interface_address.value == expected_interface


def test_udp_socket_options() -> None:
    _test_udp_socket_options("udp_socket", "127.0.0.1")


def test_multicast_udp_socket_options() -> None:
    _test_udp_socket_options("multicast_udp_socket", "239.22.0.2")


def _test_udp_socket_repeated_options(socket_type: str, socket_address: str) -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        f"""
        use clockwork::io::var_packet;
        // Docs
        {socket_type} WithRepeatedOptions
        {{
          {_make_address_fields(socket_type, socket_address)}
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;

          options
          {{
            reuse_address: true;
            reuse_address: true;
          }}
        }}
        """,
    )

    with pytest.raises(ValueError, match="Can only specify option `reuse_address` once."):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, f"repeated_options_{socket_type}"), importer=fs_importer
        )


def test_udp_socket_repeated_options() -> None:
    _test_udp_socket_repeated_options("udp_socket", "127.0.0.1")


def test_multicast_udp_socket_repeated_options() -> None:
    _test_udp_socket_repeated_options("multicast_udp_socket", "239.22.0.2")


def test_udp_socket_multicast_group() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        multicast_udp_socket IncomingWithMulticastGroup
        {
          group_address: 239.22.0.2;
          interface_address: 127.0.0.1;
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }

        // Docs
        multicast_udp_socket OutgoingWithMulticastGroup
        {
          group_address: 239.22.0.2;
          interface_address: 127.0.0.1;
          port: 12345;
          direction: outgoing;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }

        // Docs
        multicast_udp_socket BidirectionalWithMulticastRemote
        {
          group_address: 239.22.0.2;
          interface_address: 127.0.0.1;
          port: 12345;
          remote_port: 67890;
          direction: bidirectional;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }
        """,
    )

    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "valid"), importer=fs_importer)

    socket_ir = module.inner_scope.lookup("IncomingWithMulticastGroup")
    assert isinstance(socket_ir, udp.UdpSocket)
    assert socket_ir.multicast_group is not None
    assert socket_ir.multicast_group.value == "239.22.0.2"

    socket_ir = module.inner_scope.lookup("OutgoingWithMulticastGroup")
    assert isinstance(socket_ir, udp.UdpSocket)
    assert socket_ir.multicast_group is not None
    assert socket_ir.multicast_group.value == "239.22.0.2"

    socket_ir = module.inner_scope.lookup("BidirectionalWithMulticastRemote")
    assert isinstance(socket_ir, udp.UdpSocket)
    assert socket_ir.multicast_group is not None
    assert socket_ir.multicast_group.value == "239.22.0.2"


def test_udp_socket_invalid_multicast_address() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        multicast_udp_socket BadMulticastGroup
        {
          group_address: 10.0.0.1;
          interface_address: 127.0.0.1;
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }
        """,
    )

    with pytest.raises(ValueError, match="10.0.0.1 is not a multicast address."):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "invalid_multicast_address"), importer=fs_importer)


def test_udp_socket_bidirectional_multicast_missing_remote_port() -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        """
        use clockwork::io::var_packet;
        // Docs
        multicast_udp_socket BadMulticastGroup
        {
          group_address: 239.22.0.2;
          interface_address: 127.0.0.1;
          port: 12345;
          direction: bidirectional;
          message_type: Tachyon<var_packet::VarPacket<4>>;
        }
        """,
    )

    with pytest.raises(ValueError, match="Need to specify remote_port for bidirectional socket"):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, "bidirectional_multicast_missing_remote_port"), importer=fs_importer
        )


def _test_udp_batch_size(socket_type: str, socket_address: str) -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        f"""
        use clockwork::io::var_packet;
        // Docs
        {socket_type} IncomingWithBatchSize
        {{
          {_make_address_fields(socket_type, socket_address)}
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          batch_size: 7;
        }}
        """,
    )

    module = compiler.compile_source_text(
        source, ModuleID(CLK_REPO, f"valid_batch_size_{socket_type}"), importer=fs_importer
    )

    socket_ir = module.inner_scope.lookup("IncomingWithBatchSize")
    assert isinstance(socket_ir, udp.UdpSocket)
    assert isinstance(socket_ir.batch_size, primitive.DecimalValue)
    assert socket_ir.batch_size.value == 7


def test_udp_batch_size() -> None:
    _test_udp_batch_size("udp_socket", "127.0.0.1")


def test_multicast_udp_batch_size() -> None:
    _test_udp_batch_size("multicast_udp_socket", "239.22.0.2")


def _test_invalid_udp_batch_size(socket_type: str, socket_address: str) -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        f"""
        use clockwork::io::var_packet;
        // Docs
        {socket_type} IncomingWithBatchSize
        {{
          {_make_address_fields(socket_type, socket_address)}
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          batch_size: "hello world";
        }}
        """,
    )

    with pytest.raises(TypeError, match="Type inference failed: ::String != ::UInt32"):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, f"invalid_batch_size_{socket_type}"), importer=fs_importer
        )


def test_invalid_udp_batch_size() -> None:
    _test_invalid_udp_batch_size("udp_socket", "127.0.0.1")


def test_invalid_multicast_udp_batch_size() -> None:
    _test_invalid_udp_batch_size("multicast_udp_socket", "239.22.0.2")


def _test_incompatible_udp_batch_size(socket_type: str, socket_address: str) -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        f"""
        use clockwork::io::var_packet;
        // Docs
        {socket_type} IncomingWithBatchSize
        {{
          {_make_address_fields(socket_type, socket_address)}
          port: 12345;
          direction: outgoing;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          batch_size: 7;
        }}
        """,
    )

    with pytest.raises(ValueError, match="Batch size parameter is only valid for incoming UDP sockets."):
        compiler.compile_source_text(
            source, ModuleID(CLK_REPO, f"incompatible_batch_size_{socket_type}"), importer=fs_importer
        )


def test_incompatible_udp_batch_size() -> None:
    _test_incompatible_udp_batch_size("udp_socket", "127.0.0.1")


def test_incompatible_multicast_udp_batch_size() -> None:
    _test_incompatible_udp_batch_size("multicast_udp_socket", "239.22.0.2")


def _test_invalid_bind(socket_type: str, socket_address: str) -> None:
    fs_importer = importer.FilesystemImporter(compile_fn=compiler.compile_source_file)
    source = dedent(
        f"""
        use clockwork::io::var_packet;
        // Docs
        {socket_type} IncomingWithBatchSize
        {{
          {_make_address_fields(socket_type, socket_address)}
          port: 12345;
          direction: incoming;
          message_type: Tachyon<var_packet::VarPacket<4>>;
          options
          {{
            bind_to_interface: "hello world";
          }}
        }}
        """,
    )

    with pytest.raises(TypeError, match="bind_to_interface should be `true` or `false`"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, f"invalid_bind{socket_type}"), importer=fs_importer)


def test_invalid_bind() -> None:
    _test_invalid_bind("udp_socket", "127.0.0.1")


def test_invalid_multicast_bind() -> None:
    _test_invalid_bind("multicast_udp_socket", "239.22.0.2")
