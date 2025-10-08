# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""UDP endpoints."""

from __future__ import annotations

import ipaddress
from dataclasses import dataclass
from decimal import Decimal
from enum import Enum
from typing import Final, TypeAlias

from clockwork.dsl import cst
from clockwork.dsl.ir import clkbuiltins, expr, node, primitive, typesys, uuid_reg
from clockwork.dsl.ir.cst_util import get_span
from typing_extensions import override


class IODirection(Enum):
    """Direction for an IO endpoint."""

    incoming = 0
    outgoing = 1
    bidirectional = 2


@dataclass
class SocketReceiveBuffer(node.CstNode[cst.SocketReceiveBuffer]):
    """Option for SO_RECVBUF."""

    value_expr: expr.Expr
    resolved_value: primitive.UnitLiteral | None

    @classmethod
    def from_cst(
        cls: type[SocketReceiveBuffer], cst_node: cst.SocketReceiveBuffer, module: node.Module
    ) -> SocketReceiveBuffer:
        """Construct a SocketReceiveBuffer IR node form a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        receive_buffer = expr.Expr.from_cst(cst_node.child_typespec(), module)

        return SocketReceiveBuffer(
            cst_node=cst_node,
            module=module,
            value_expr=receive_buffer,
            resolved_value=None,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if self.resolved_value is not None:
            msg = "Trying to resolve SocketReceiveBuffer twice."
            raise TypeError(msg)

        value = self.value_expr.evaluate()
        if not isinstance(value, primitive.UnitLiteral) or value.type_info is not clkbuiltins.BYTES:
            msg = node.append_error_line(self.cst_node, self.module, "receive_buffer should be a bytes literal")
            raise TypeError(msg)

        self.resolved_value = value

    @property
    def value(self) -> primitive.UnitLiteral:
        """Convenience function to get the resolved value."""
        if self.resolved_value is None:
            msg = "SocketReceiveBuffer has not been resolved."
            raise TypeError(msg)
        return self.resolved_value


@dataclass
class SocketReuseAddress(node.CstNode[cst.SocketReuseAddress]):
    """Option for SO_REUSEADDR."""

    value_expr: expr.Expr
    resolved_value: typesys.NamedValue | None

    @classmethod
    def from_cst(
        cls: type[SocketReuseAddress], cst_node: cst.SocketReuseAddress, module: node.Module
    ) -> SocketReuseAddress:
        """Construct a SocketReuseAddress IR node form a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        reuse_address = expr.Expr.from_cst(cst_node.child_typespec(), module)

        return SocketReuseAddress(
            cst_node=cst_node,
            module=module,
            value_expr=reuse_address,
            resolved_value=None,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if self.resolved_value is not None:
            msg = "Trying to resolve SocketReuseAddress twice."
            raise TypeError(msg)

        value = self.value_expr.evaluate()
        if value not in (clkbuiltins.TRUE_VALUE, clkbuiltins.FALSE_VALUE):
            msg = node.append_error_line(self.cst_node, self.module, "reuse_address should be `true` or `false`")
            raise TypeError(msg)
        assert isinstance(value, typesys.NamedValue)

        self.resolved_value = value

    @property
    def value(self) -> typesys.NamedValue:
        """Convenience function to get the resolved value."""
        if self.resolved_value is None:
            msg = "SocketReuseAddress has not been resolved."
            raise TypeError(msg)
        return self.resolved_value


@dataclass
class SocketBindToDevice(node.CstNode[cst.SocketBindToInterface]):
    """Option for SO_BINDTODEVICE."""

    value_expr: expr.Expr
    resolved_value: typesys.NamedValue | None
    interface_address: primitive.IPv4Address

    @classmethod
    def from_cst(
        cls: type[SocketBindToDevice],
        cst_node: cst.SocketBindToInterface,
        interface_address: primitive.IPv4Address,
        module: node.Module,
    ) -> SocketBindToDevice:
        """Construct a SocketBindToDevice IR node form a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        return SocketBindToDevice(
            cst_node=cst_node,
            module=module,
            value_expr=expr.Expr.from_cst(cst_node.child_typespec(), module),
            resolved_value=None,
            interface_address=interface_address,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if self.resolved_value is not None:
            msg = "Trying to resolve SocketBindToDevice twice."
            raise TypeError(msg)

        value = self.value_expr.evaluate()
        if value not in (clkbuiltins.TRUE_VALUE, clkbuiltins.FALSE_VALUE):
            msg = node.append_error_line(self.cst_node, self.module, "bind_to_interface should be `true` or `false`")
            raise TypeError(msg)
        assert isinstance(value, typesys.NamedValue)

        self.resolved_value = value

    @property
    def value(self) -> typesys.NamedValue:
        """Convenience function to get the resolved value."""
        if self.resolved_value is None:
            msg = "SocketBindToDevice has not been resolved."
            raise TypeError(msg)
        return self.resolved_value


@dataclass
class SocketMulticastGroup:
    """Option for IP_ADD_MEMBERSHIP. Only to be added implicitly when sockets are configured for multicast."""

    group_address: primitive.IPv4Address
    interface_address: primitive.IPv4Address


@dataclass
class SocketMulticastInterface:
    """Option for IP_MULTICAST_IF. Only to be added implicitly when sockets are configured for multicast."""

    interface_address: primitive.IPv4Address


@dataclass
class SocketMulticastLoop:
    """Option for IP_MULTICAST_LOOP. Only to be added implicitly when sockets are configured for multicast."""

    value: bool


SocketOptionBase: Final[TypeAlias] = type
"""An alias for any socket option type.  Not sure why this is needed..."""

SocketOptionsDict: Final[TypeAlias] = dict[
    SocketOptionBase,
    SocketReuseAddress
    | SocketReceiveBuffer
    | SocketMulticastGroup
    | SocketMulticastInterface
    | SocketMulticastLoop
    | SocketBindToDevice,
]


@dataclass
class UdpSocketOptions(node.CstNode[cst.UdpSocketOptions]):
    """Options for a udp socket."""

    options: SocketOptionsDict  # pyright: ignore[reportInvalidTypeForm] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    @classmethod
    def from_cst(
        cls: type[UdpSocketOptions],
        cst_node: cst.UdpSocketOptions,
        local_address: primitive.IPv4Address,
        module: node.Module,
    ) -> UdpSocketOptions:
        """Construct a UdpSocketOptions IR node form a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)

        options: SocketOptionsDict = {}  # pyright: ignore[reportInvalidTypeForm] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip
        for child_cst in cst_node.children_udp_socket_option():
            if reuse_address_cst := child_cst.maybe_socket_reuse_address():
                if SocketReuseAddress in options:
                    msg = node.append_error_line(cst_node, module, "Can only specify option `reuse_address` once.")
                    raise ValueError(msg)
                options[SocketReuseAddress] = SocketReuseAddress.from_cst(reuse_address_cst, module)
            elif receive_buffer_cst := child_cst.maybe_socket_receive_buffer():
                if SocketReceiveBuffer in options:
                    msg = node.append_error_line(cst_node, module, "Can only specify option `receive_buffer` once.")
                    raise ValueError(msg)
                options[SocketReceiveBuffer] = SocketReceiveBuffer.from_cst(receive_buffer_cst, module)
            elif bind_cst := child_cst.maybe_socket_bind_to_interface():
                if SocketBindToDevice in options:
                    msg = node.append_error_line(cst_node, module, "Can only specify option `bind_to_interface` once.")
                    raise ValueError(msg)
                options[SocketBindToDevice] = SocketBindToDevice.from_cst(bind_cst, local_address, module)
            else:
                msg = node.append_error_line(
                    cst_node, module, f"Unable to handle socket option for cst node: {type(child_cst)}"
                )
                raise ValueError(msg)

        return UdpSocketOptions(
            cst_node=cst_node,
            module=module,
            options=options,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        for value in self.options.values():
            value.resolve()


def create_class_endpoints(
    direction: IODirection, module: node.Module, scope: node.Scope
) -> tuple[UdpSocketEndpoint | None, UdpSocketEndpoint | None]:
    """Create class endpoint types depending on the IO direction."""
    if direction in (IODirection.incoming, IODirection.bidirectional):
        producer_endpoint = UdpSocketEndpoint(
            name="producer_endpoint",
            scope=scope,
            type_info=clkbuiltins.UDP_SOCKET_ENDPOINT_TYPE,
        )
        uuid_reg.register_entity_with_stable_key(module.context, producer_endpoint)
    else:
        producer_endpoint = None

    if direction in (IODirection.outgoing, IODirection.bidirectional):
        observer_endpoint = UdpSocketEndpoint(
            name="observer_endpoint",
            scope=scope,
            type_info=clkbuiltins.UDP_SOCKET_ENDPOINT_TYPE,
        )
        uuid_reg.register_entity_with_stable_key(module.context, observer_endpoint)
    else:
        observer_endpoint = None

    return producer_endpoint, observer_endpoint


@dataclass
class UdpSocket(
    node.CstNode[cst.UdpSocket | cst.MulticastUdpSocket],
    node.DocRequiredEntity,
    typesys.TypeDef,
    typesys.InstantiatableEntity,
):
    """A udp socket endpoint."""

    inner_scope: node.Scope

    address: primitive.IPv4Address
    port: primitive.DecimalValue
    direction: IODirection
    message_type: expr.TypeExpression | typesys.TypeVal
    # OI-2203 follow-up to unify local and remote terminology
    remote_address: primitive.IPv4Address | None
    remote_port: primitive.DecimalValue | None
    batch_size: expr.Expr | primitive.DecimalValue | None
    multicast_group: primitive.IPv4Address | None
    options: UdpSocketOptions | None

    producer_endpoint: UdpSocketEndpoint | None
    observer_endpoint: UdpSocketEndpoint | None

    @classmethod
    def from_cst(
        cls: type[UdpSocket], cst_node: cst.UdpSocket | cst.MulticastUdpSocket, module: node.Module
    ) -> UdpSocket:
        """Construct a UdpSocket IR node form a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.from_cst(cst_node.child_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        address = _get_socket_address(cst_node, module)
        port = primitive.DecimalLiteral.from_child_cst(
            cst_node.child_udp_socket_port().child_value().child_number(),
            cst_node.child_udp_socket_port().child_value(),
            module,
        )
        typesys.unify(port.type_info, clkbuiltins.UINT16)
        multicast_group = _maybe_get_multicast_group(cst_node, module)

        message_type = expr.TypeExpression.make(
            expr.Expr.from_cst(cst_node.child_udp_socket_message_type().child_typespec(), module)
        )

        direction_str = get_span(cst_node.child_udp_socket_direction().child_value(), module.terminals)
        try:
            direction = IODirection[direction_str]
        except KeyError:
            # This should only fire if the fltk and IODirection enum don't match.
            msg = node.append_error_line(cst_node, module, f"Invalid IO direction: {direction_str}")
            raise ValueError(msg) from None

        # Handle remote address and port.  Conditions:
        # - If bidirectional, parse remote address and port
        # - If not bidrectional, presence of remote address or port is invalid and user should be notified
        remote_address = None
        remote_port = None
        if direction == IODirection.bidirectional:
            remote_address, remote_port = _get_remote_endpoint_fields(cst_node, multicast_group, module)
        else:
            # Not bidirectional, so remote address and port should not be present
            if isinstance(cst_node, cst.UdpSocket) and cst_node.maybe_udp_socket_remote_address():
                msg = node.append_error_line(
                    cst_node, module, "direction is not bidirectional; should not specify remote_address"
                )
                raise ValueError(msg)
            if cst_node.maybe_udp_socket_remote_port():
                msg = node.append_error_line(
                    cst_node, module, "direction is not bidirectional; should not specify remote_port"
                )
                raise ValueError(msg)

        batch_size: expr.Expr | primitive.DecimalValue | None = None
        if batch_size_cst := cst_node.maybe_udp_socket_batch_size():
            if direction != IODirection.incoming:
                msg = node.append_error_line(
                    batch_size_cst, module, "Batch size parameter is only valid for incoming UDP sockets."
                )
                raise ValueError(msg)
            batch_size = expr.Expr.from_cst(batch_size_cst.child_value(), module)
            typesys.unify(batch_size.type_info, clkbuiltins.UINT32)
        elif direction == IODirection.incoming:
            batch_size = primitive.DecimalValue(type_info=clkbuiltins.UINT32, value=Decimal(1))

        options: UdpSocketOptions | None = None
        if options_cst := cst_node.maybe_udp_socket_options():
            options = UdpSocketOptions.from_cst(options_cst, address, module)

        inner_scope = module.inner_scope.make_child_scope(name)
        producer_endpoint, observer_endpoint = create_class_endpoints(direction, module, inner_scope)

        return UdpSocket(
            doc=doc,
            name=name,
            scope=module.inner_scope,
            module=module,
            cst_node=cst_node,
            type_info=clkbuiltins.TYPE_TYPE,
            inner_scope=inner_scope,
            address=address,
            port=port,
            direction=direction,
            message_type=message_type,
            remote_address=remote_address,
            remote_port=remote_port,
            batch_size=batch_size,
            multicast_group=multicast_group,
            options=options,
            observer_endpoint=observer_endpoint,
            producer_endpoint=producer_endpoint,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        assert isinstance(self.message_type, expr.TypeExpression)
        message_type = self.message_type.evaluate()
        if not isinstance(message_type, typesys.Instantiation) or message_type.instantiates is not clkbuiltins.TACHYON:
            msg = self.message_type.append_error_line("Only Tachyon message representations are supported here.")
            raise ValueError(msg)
        self.message_type = message_type

        uuid_reg.register_entity_with_stable_key(self.module.context, self)

        if isinstance(self.batch_size, expr.Expr):
            batch_size = self.batch_size.evaluate()
            if not isinstance(batch_size, primitive.DecimalValue):
                msg = self.batch_size.append_error_line("Expected batch_size to resolve to a decimal value.")
                raise TypeError(msg)
            self.batch_size = batch_size

        if self.options:
            self.options.resolve()

    @property
    def endpoint(self) -> str:
        """Socket endpoint as a string."""
        address_strs = [
            self.address.value,
            f"{self.port.value}",
        ]
        if self.direction == IODirection.bidirectional:
            assert isinstance(self.remote_address, primitive.IPv4Address)
            assert isinstance(self.remote_port, primitive.DecimalValue)
            address_strs.extend(
                [
                    self.remote_address.value,
                    f"{self.remote_port.value}",
                ]
            )
        return f"{self.direction.name}({':'.join(address_strs)})"

    @override
    def make_instance(
        self, *, cst_node: cst.NewStmt | None, module: node.Module, scope: node.Scope, name: str, doc: node.Doc | None
    ) -> UdpSocketInstance:
        """Create an instance of the entity."""
        return UdpSocketInstance.make(socket=self, cst_node=cst_node, module=module, scope=scope, name=name, doc=doc)


def _get_socket_address(cst_node: cst.UdpSocket | cst.MulticastUdpSocket, module: node.Module) -> primitive.IPv4Address:
    if isinstance(cst_node, cst.UdpSocket):
        return primitive.IPv4Address.from_cst(cst_node.child_udp_socket_address().child_value(), module)

    return primitive.IPv4Address.from_cst(cst_node.child_multicast_udp_socket_interface_address().child_value(), module)


def _get_remote_endpoint_fields(
    cst_node: cst.UdpSocket | cst.MulticastUdpSocket, multicast_group: primitive.IPv4Address | None, module: node.Module
) -> tuple[primitive.IPv4Address | None, primitive.DecimalLiteral | None]:
    remote_address = None
    remote_port = None

    if isinstance(cst_node, cst.UdpSocket):
        if (remote_address_child := cst_node.maybe_udp_socket_remote_address()) is None:
            msg = node.append_error_line(cst_node, module, "Need to specify remote_address for bidirectional socket.")
            raise ValueError(msg)
        remote_address = primitive.IPv4Address.from_cst(remote_address_child.child_value(), module)
    else:
        remote_address = multicast_group

    if (remote_port_child := cst_node.maybe_udp_socket_remote_port()) is None:
        msg = node.append_error_line(cst_node, module, "Need to specify remote_port for bidirectional socket.")
        raise ValueError(msg)
    remote_port = primitive.DecimalLiteral.from_child_cst(
        remote_port_child.child_value().child_number(),
        remote_port_child.child_value(),
        module,
    )
    typesys.unify(remote_port.type_info, clkbuiltins.UINT16)

    return remote_address, remote_port


def _maybe_get_multicast_group(
    cst_node: cst.UdpSocket | cst.MulticastUdpSocket, module: node.Module
) -> primitive.IPv4Address | None:
    if isinstance(cst_node, cst.UdpSocket):
        return None

    group_address = primitive.IPv4Address.from_cst(
        cst_node.child_multicast_udp_socket_group_address().child_value(), module
    )
    if not ipaddress.ip_address(group_address.value).is_multicast:
        msg = node.append_error_line(
            cst_node.child_multicast_udp_socket_group_address(),
            module,
            f"{group_address.value} is not a multicast address.",
        )
        raise ValueError(msg)

    return group_address


@dataclass
class UdpSocketEndpoint(typesys.NamedValue):
    """An endpoint of a UdpSocket instance.

    Sockets can have 1 or 2 endpoints.  These need to be distinguished
    because at system generation time each connection must reference
    what it's connected to and there cannot be duplicates.

    This is a dummy class to serve as the endpoint class type.
    """


@dataclass
class UdpSocketEndpointInstance(typesys.NamedValue):
    """An endpoint of a UdpSocket instance.

    Sockets can have 1 or 2 endpoints.  These need to be distinguished
    because at system generation time each connection must reference
    what it's connected to and there cannot be duplicates.

    This differs from the UdpSocketEndpoint as this is an instance of
    that endpoint.

    """

    socket: UdpSocket
    endpoint: UdpSocketEndpoint


@dataclass
class UdpSocketInstance(node.CstNode[cst.NewStmt], node.DocableEntity, typesys.NamedAttribute):
    """An instantiation of a udp socket endpoint."""

    socket: UdpSocket
    inner_scope: node.Scope
    observer_endpoint: UdpSocketEndpointInstance | None
    producer_endpoint: UdpSocketEndpointInstance | None

    # We have to suppress PLR0913 (too many args) because this is already an extremely simple function
    # that can't be split but still needs all these args. The args are all different types so mypy will
    # catch any mixups in the call sites, and we have made the args kwonly as extra assurance.
    @classmethod
    def make(  # noqa: PLR0913
        cls: type[UdpSocketInstance],
        *,
        socket: UdpSocket,
        cst_node: cst.NewStmt | None,
        module: node.Module,
        scope: node.Scope,
        name: str,
        doc: node.Doc | None,
    ) -> UdpSocketInstance:
        """Factory function for UdpSocketInstance."""
        inner_scope = socket.inner_scope.make_dynamic_scope(path_parent=scope, name=name)

        if socket.producer_endpoint:
            producer_endpoint = UdpSocketEndpointInstance(
                name="producer_endpoint_instance",
                scope=inner_scope,
                socket=socket,
                endpoint=socket.producer_endpoint,
                type_info=clkbuiltins.UDP_SOCKET_ENDPOINT_INSTANCE_TYPE,
            )
        else:
            producer_endpoint = None

        if socket.observer_endpoint:
            observer_endpoint = UdpSocketEndpointInstance(
                name="observer_endpoint_instance",
                scope=inner_scope,
                socket=socket,
                endpoint=socket.observer_endpoint,
                type_info=clkbuiltins.UDP_SOCKET_ENDPOINT_INSTANCE_TYPE,
            )
        else:
            observer_endpoint = None

        return cls(
            name=name,
            scope=scope,
            inner_scope=inner_scope,
            type_info=clkbuiltins.UDP_SOCKET_INSTANCE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            socket=socket,
            observer_endpoint=observer_endpoint,
            producer_endpoint=producer_endpoint,
        )


def validate_message_type(message_type: typesys.TypeVal, socket: UdpSocket, channel_name: str) -> str | None:
    """Check for the expected UDP message type."""
    if message_type.value_key() != socket.message_type.value_key():
        return f"Mismatched 'message_type' between socket '{socket.name}' and channel '{channel_name}'."
    return None
