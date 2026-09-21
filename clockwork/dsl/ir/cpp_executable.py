# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""CppTarget-related IR nodes."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from textwrap import dedent
from typing import TYPE_CHECKING, Any, Final

from clockwork.dsl import clockwork_cst_protocol as cst
from clockwork.dsl.bazel.targets import Label, get_bazel_label_for_clk_label, get_bazel_label_for_python_type
from clockwork.dsl.cog.cpp_python_cog import CppPythonCog as PythonCogImplGenerator
from clockwork.dsl.cog.cppcog import Cog as CogGenerator
from clockwork.dsl.cog.cppcog import InstantiatedCog as InstantiatedCogGenerator
from clockwork.dsl.cog.cppcog import to_dial_name
from clockwork.dsl.cpp import typereg, types, values
from clockwork.dsl.cpp.context import (
    CppChunk,
    CppModuleChunks,
    Header,
    SystemHeader,
    as_cc_binary,
    as_cc_binary_with_embedded_py,
    write_to_file,
)
from clockwork.dsl.ir import (
    audio,
    clkbuiltins,
    diagnostics,
    expr,
    node,
    primitive,
    proto_to_tap,
    schema_reg,
    statement,
    typesys,
    udp,
    units,
    uuid_reg,
)
from clockwork.dsl.ir.cog import Cog, InstantiatedCog
from clockwork.dsl.ir.conversion_utils import protobuf_repr_to_cpp_type
from clockwork.dsl.ir.cst_util import get_span
from clockwork.dsl.ir.extern_type import ExternType
from clockwork.dsl.ir.interface import InterfaceReference
from clockwork.dsl.ir.module_id import CLK_REPO, JEWELS_REPO
from clockwork.dsl.ir.path_resolver import BazelPathResolver
from clockwork.dsl.ir.representation import RepresentationReference
from clockwork.dsl.ir.uuid_reg import lookup_uuid

if TYPE_CHECKING:
    from collections.abc import Sequence
    from pathlib import Path
    from uuid import UUID

    from clockwork.dsl.bazel.cc_targets import CcBinary, CcBinaryWithEmbeddedPy
    from clockwork.dsl.compiler_context import CompilerContext


_SOCKET_ENDPOINT_TYPE: Final = types.CppType(
    includes=[Header(CLK_REPO, "jewels/networking/socket_endpoint.hh")],
    type_name="SocketEndpoint",
    cpp_namespace="jewels::networking",
)

_ENDPOINT_CLASS_ID_TYPE: Final = types.CppType(
    includes=[Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh")],
    type_name="EndpointClassId",
    cpp_namespace="clockwork::common",
)


@dataclass(eq=True, slots=True)
class CppCog:
    """Instantiates a cog type inside cpp_target."""

    cog_ir: Cog | expr.Expr
    dial_header: Header | None
    cog_header: Header | None

    @classmethod
    def from_cst(
        cls: type[CppCog],
        cst_node: cst.CppCog | cst.CppPythonCog | cst.PyPythonCogDial,
        module: node.Module,
    ) -> CppCog:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(cog_ir=typespec, dial_header=None, cog_header=None)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.cog_ir, expr.Expr):
            msg = f"Attempt to resolve CppCog twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.cog_ir.evaluate()
        if not isinstance(typespec, Cog):
            msg = self.cog_ir.append_error_line(f"Expected Cog, got {type(typespec)}")
            raise TypeError(msg)
        self.cog_ir = typespec

    # ARG002 suppressed because compiler_context not used here but it's part of the standard interface
    def render(self, compiler_context: CompilerContext, namespace: str) -> CppModuleChunks:  # noqa: ARG002 (see above)
        """Convert the cog to C++."""
        if not isinstance(self.cog_ir, Cog):
            msg = "Attempt to render before resolving."
            raise TypeError(msg)
        if not self.dial_header:
            msg = "Dial header was not resolved."
            raise TypeError(msg)
        if not self.cog_header:
            msg = "Cog header was not resolved."
            raise TypeError(msg)

        dial_class_name = to_dial_name(self.cog_ir.name)

        cog_gen = CogGenerator(
            cog_ir=self.cog_ir,
            class_name=self.cog_ir.name,
            dial_name=dial_class_name,
            header_name=None,
            cpp_namespace=namespace,
            dial_header=self.dial_header,
        )
        return cog_gen.render()


@dataclass(eq=True, slots=True)
class CppInstantiatedCog:
    """Instantiates a cog type inside cpp_target."""

    instantiation: InstantiatedCog
    dial_header: Header | None
    cog_header: Header | None

    # ARG002 suppressed because compiler_context not used here but it's part of the standard interface
    def render(self, compiler_context: CompilerContext, namespace: str) -> CppModuleChunks:  # noqa: ARG002 (see above)
        """Convert the cog to C++."""
        if not self.dial_header:
            msg = "Dial header was not resolved."
            raise TypeError(msg)
        if not self.cog_header:
            msg = "Cog header was not resolved."
            raise TypeError(msg)

        dial_class_name = to_dial_name(self.instantiation.name)

        instantiation_gen = InstantiatedCogGenerator(
            instantiation=self.instantiation,
            class_name=self.instantiation.cog_ir.name,
            dial_name=dial_class_name,
            header_name=None,
            cpp_namespace=namespace,
            dial_header=self.dial_header,
        )
        return instantiation_gen.render()


@dataclass(eq=True, slots=True)
class CppPythonCog:
    """A wrapper around a CppCog to render the implementation for python cogs."""

    cst_node: cst.CppPythonCog | cst.NewStmt | None
    module: node.Module
    cpp_cog: CppCog

    def get_py_deps(self) -> list[Label]:
        """Get the python dependency labels for this instance."""
        if not isinstance(self.cpp_cog.cog_ir, Cog):
            # This is resolved by the CppCog.  No need for an extra resolve here.
            msg = "Attempt to render before resolving."
            raise TypeError(msg)
        if not self.cpp_cog.cog_ir.python_options:
            msg = node.append_error_line(
                self.cst_node, self.module, "Python options are required to instantiate python cog"
            )
            raise ValueError(msg)
        assert isinstance(self.cpp_cog.cog_ir.python_options.python_dial_class_name, str)
        py_deps: list[Label] = []
        if py_dep := get_bazel_label_for_python_type(self.cpp_cog.cog_ir.python_options.python_dial_class_name):
            py_deps.append(py_dep)
        assert isinstance(self.cpp_cog.cog_ir.python_options.python_impl_class_name, str)
        if py_dep := get_bazel_label_for_python_type(self.cpp_cog.cog_ir.python_options.python_impl_class_name):
            py_deps.append(py_dep)
        return py_deps

    def render(self, namespace: str) -> CppModuleChunks:
        """Generate the cog implementation in C++."""
        if not isinstance(self.cpp_cog.cog_ir, Cog):
            # This is resolved by the CppCog.  No need for an extra resolve here.
            msg = "Attempt to render before resolving."
            raise TypeError(msg)
        assert not self.cpp_cog.cog_ir.is_generic()
        if not self.cpp_cog.cog_ir.python_options:
            msg = node.append_error_line(
                self.cst_node, self.module, "Python options required to instantiate python cog"
            )
            raise ValueError(msg)
        assert self.cpp_cog.dial_header
        assert self.cpp_cog.cog_ir.python_options
        assert isinstance(self.cpp_cog.cog_ir.python_options.python_dial_class_name, str)
        assert isinstance(self.cpp_cog.cog_ir.python_options.python_impl_class_name, str)

        dial_class_name = to_dial_name(self.cpp_cog.cog_ir.name)
        impl_gen = PythonCogImplGenerator(
            cog_ir=self.cpp_cog.cog_ir,
            dial_class_name=dial_class_name,
            python_dial_class_name=self.cpp_cog.cog_ir.python_options.python_dial_class_name,
            python_impl_class_name=self.cpp_cog.cog_ir.python_options.python_impl_class_name,
            cpp_namespace=namespace,
            dial_header=self.cpp_cog.dial_header,
        )
        return impl_gen.render()


@dataclass
class CppSocketOptions:
    """Renders socket options."""

    options: udp.SocketOptionsDict  # pyright: ignore[reportInvalidTypeForm] # TODO(DX-2313): Address pyright errors ignored to migrate from mypy # fmt: skip

    def render(self) -> list[types.CppValue]:
        """Render options to CppValue s."""
        cpp_option_value_template = types.CppTemplate(
            includes=[Header(CLK_REPO, "clockwork/pinion/sock_opt.hh")],
            template_name="SockOptionValue",
            cpp_namespace="clockwork::pinion",
        )

        networking_sock_opt_header = Header(JEWELS_REPO, "jewels/networking/sock_opt.hh")
        cpp_option_enum_type = types.CppType(
            includes=[networking_sock_opt_header],
            type_name="SockOption",
            cpp_namespace="jewels::networking",
        )

        options: list[types.CppValue] = []
        for value in self.options.values():
            if isinstance(value, udp.SocketReuseAddress):
                cpp_option_enum = types.CppScopedValue(
                    scope=cpp_option_enum_type,
                    header=[networking_sock_opt_header],
                    name="so_reuse_address",
                )
                cpp_option_value = str(int(primitive.value_to_bool(value.value)))
            elif isinstance(value, udp.SocketReceiveBuffer):
                cpp_option_enum = types.CppScopedValue(
                    scope=cpp_option_enum_type,
                    header=[networking_sock_opt_header],
                    name="so_receive_buffer",
                )
                cpp_option_value = str(value.value.as_unit(units.BYTE).value)
            elif isinstance(value, udp.SocketBindToDevice) and value.value is not None:
                iface_name_or_ip_address = value.value
                type_name = (
                    "AddressView"
                    if isinstance(iface_name_or_ip_address, primitive.IPv4Address)
                    else "InterfaceNameView"
                )
                cpp_option_enum = types.CppScopedValue(
                    scope=cpp_option_enum_type,
                    header=[networking_sock_opt_header],
                    name="so_bind_to_device",
                )
                cpp_option_value = types.CppValue(
                    value_type=types.CppType(
                        includes=[networking_sock_opt_header],
                        type_name=type_name,
                        cpp_namespace="jewels::networking",
                    ),
                    value=f'"{iface_name_or_ip_address.value}"',
                )
            elif isinstance(value, udp.SocketMulticastGroup):
                cpp_option_enum = types.CppScopedValue(
                    scope=cpp_option_enum_type,
                    header=[networking_sock_opt_header],
                    name="ip_add_membership",
                )
                cpp_option_value = (
                    f'.group_address="{value.group_address.value}", .local_address="{value.interface_address.value}"'
                )
            elif isinstance(value, udp.SocketMulticastInterface):
                cpp_option_enum = types.CppScopedValue(
                    scope=cpp_option_enum_type,
                    header=[networking_sock_opt_header],
                    name="ip_multicast_if",
                )
                cpp_option_value = f'.interface_address="{value.interface_address.value}"'
            elif isinstance(value, udp.SocketMulticastLoop):
                cpp_option_enum = types.CppScopedValue(
                    scope=cpp_option_enum_type,
                    header=[networking_sock_opt_header],
                    name="ip_multicast_loop",
                )
                cpp_option_value = f"{int(value.value)}"
            else:
                msg = f"Unable to render socket option of type: {type(value)}"
                raise TypeError(msg)

            options.append(
                types.CppValue(
                    value_type=cpp_option_value_template.instantiate([cpp_option_enum]),
                    value=cpp_option_value,
                )
            )

        return options


def construct_uuid_value(tag_type: types.CppType, uuid: UUID) -> types.CppValue:
    """Construct a UUID c++ value expression from a python UUID."""
    return types.CppValue(
        types.UUID.instantiate([tag_type]),
        types.CppValue(
            types.ARRAY.instantiate([types.UINT8, types.CppValue(None, "16U")]),
            ", ".join(f"{byte:#04x}" for byte in uuid.bytes),
        ),
    )


@dataclass(eq=True, slots=True)
class CppUdpSocket:
    """Instantiates a udp socket type inside cpp_target."""

    udp_socket_ir: udp.UdpSocket | expr.Expr
    options: CppSocketOptions | None

    @classmethod
    def from_cst(
        cls: type[CppUdpSocket],
        cst_node: cst.CppUdpSocket,
        module: node.Module,
    ) -> CppUdpSocket:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(udp_socket_ir=typespec, options=None)

    @classmethod
    def from_generate_cpp(cls: type[CppUdpSocket], udp_socket_ir: udp.UdpSocket) -> CppUdpSocket:
        """Create an IR node for an auto generated CppTarget."""
        result = cls(
            udp_socket_ir=udp_socket_ir,
            options=CppSocketOptions(udp_socket_ir.options.options) if udp_socket_ir.options else None,
        )
        result._resolve_multicast_group()
        return result

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.udp_socket_ir, expr.Expr):
            msg = f"Attempt to resolve CppUdpSocket twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.udp_socket_ir.evaluate()
        if not isinstance(typespec, udp.UdpSocket):
            msg = self.udp_socket_ir.append_error_line(f"Expected UdpSocket, got {type(typespec)}")
            raise TypeError(msg)
        self.udp_socket_ir = typespec
        if self.udp_socket_ir.options:
            self.options = CppSocketOptions(self.udp_socket_ir.options.options)

        self._resolve_multicast_group()

    def _resolve_multicast_group(self) -> None:
        assert isinstance(self.udp_socket_ir, udp.UdpSocket)
        if self.udp_socket_ir.multicast_group is not None:
            if self.options is None:
                self.options = CppSocketOptions(udp.SocketOptionsDict({}))

            if self.udp_socket_ir.direction in (udp.IODirection.incoming, udp.IODirection.bidirectional):
                if udp.SocketMulticastGroup in self.options.options:
                    msg = "Found unexpected SocketMulticastGroup in user socket options."
                    raise RuntimeError(msg)
                self.options.options[udp.SocketMulticastGroup] = udp.SocketMulticastGroup(
                    group_address=self.udp_socket_ir.multicast_group, interface_address=self.udp_socket_ir.address
                )
                if udp.SocketMulticastLoop in self.options.options:
                    msg = "Found unexpected SocketMulticastLoop in user socket options."
                    raise RuntimeError(msg)
                self.options.options[udp.SocketMulticastLoop] = udp.SocketMulticastLoop(False)

            if self.udp_socket_ir.direction in (udp.IODirection.outgoing, udp.IODirection.bidirectional):
                if udp.SocketMulticastInterface in self.options.options:
                    msg = "Found unexpected SocketMulticastInterface in user socket options."
                    raise RuntimeError(msg)
                self.options.options[udp.SocketMulticastInterface] = udp.SocketMulticastInterface(
                    interface_address=self.udp_socket_ir.address
                )

    def render(self, compiler_context: CompilerContext, enclosing_namespace: str) -> CppModuleChunks:
        """Convert the socket to C++."""
        if not isinstance(self.udp_socket_ir, udp.UdpSocket):
            msg = "Attempt to render before resolving."
            raise TypeError(msg)

        cpp_mod = CppModuleChunks()
        socket_type = types.CppStruct(
            name=typereg.get_cpp_type(compiler_context, self.udp_socket_ir),
            doc=self.udp_socket_ir.doc.value,
        )
        uuid_type = typereg.get_cpp_template(compiler_context, clkbuiltins.UUID).instantiate(
            [
                types.CppType(
                    [Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh")],
                    "IoConnectionClassId",
                    typereg.CLOCKWORK_NAMESPACE + "::common",
                )
            ]
        )
        socket_type.public.append(
            types.CppNamedValue(
                named_type=types.CppNamedType(
                    argument_type=uuid_type,
                    argument_name="uuid",
                ),
                value=values.uuid_to_byte_array(
                    compiler_context, uuid_reg.lookup_uuid(compiler_context, self.udp_socket_ir)
                ),
                doc="Class type UUID.",
                qualifiers=["static", "constexpr"],
            )
        )
        address = self.udp_socket_ir.address.value
        if self.udp_socket_ir.multicast_group is not None:
            # Note the change in meaning of the `address` field. It normally
            # refers to the business end of the socket, but with multicast
            # enabled it is reinterpreted as an indication of which interface
            # should be used to communicate with the multicast group.
            address = self.udp_socket_ir.multicast_group.value

        socket_type.public.append(
            types.CppNamedValue(
                named_type=types.CppNamedType(
                    argument_type=types.STRING_VIEW,
                    argument_name="host",
                ),
                value=types.CppValue(None, f'"{address}"'),
                doc="IPv4 address",
                qualifiers=["static", "constexpr"],
            ),
        )
        socket_type.public.append(
            types.CppNamedValue(
                named_type=types.CppNamedType(
                    argument_type=typereg.get_cpp_type(compiler_context, clkbuiltins.UINT16),
                    argument_name="port",
                ),
                value=types.CppValue(None, str(primitive.unsigned_decimal_to_int(self.udp_socket_ir.port))),
                doc="Destination port",
                qualifiers=["static", "constexpr"],
            ),
        )

        is_bidirectional = self.udp_socket_ir.direction == udp.IODirection.bidirectional
        if is_bidirectional:
            if not self.udp_socket_ir.remote_address:
                msg = "remote_address should not be None. The bidirectional socket is ill-formed."
                raise ValueError(msg)
            if not self.udp_socket_ir.remote_port:
                msg = "remote_port should not be None. The bidirectional socket is ill-formed."
                raise ValueError(msg)
            socket_type.public.append(
                types.CppNamedValue(
                    named_type=types.CppNamedType(
                        argument_type=types.STRING_VIEW,
                        argument_name="remote_host",
                    ),
                    value=types.CppValue(None, f'"{self.udp_socket_ir.remote_address.value}"'),
                    doc="IPv4 address",
                    qualifiers=["static", "constexpr"],
                ),
            )
            socket_type.public.append(
                types.CppNamedValue(
                    named_type=types.CppNamedType(
                        argument_type=typereg.get_cpp_type(compiler_context, clkbuiltins.UINT16),
                        argument_name="remote_port",
                    ),
                    value=types.CppValue(None, str(primitive.unsigned_decimal_to_int(self.udp_socket_ir.remote_port))),
                    doc="Remote port",
                    qualifiers=["static", "constexpr"],
                ),
            )

        io_direction = self.udp_socket_ir.direction.name
        factory_fn = CppChunk()
        socket_template = types.CppTemplate(
            includes=[Header(CLK_REPO, f"clockwork/pinion/{io_direction}_udp.hh")],
            template_name=f"{io_direction.capitalize()}Udp",
            cpp_namespace="clockwork::pinion",
        ).instantiate([typereg.get_cpp_type(compiler_context, self.udp_socket_ir.message_type)])

        endpoint_args = [
            construct_uuid_value(
                _ENDPOINT_CLASS_ID_TYPE,
                uuid_reg.lookup_uuid(compiler_context, endpoint),
            )
            for endpoint in (self.udp_socket_ir.producer_endpoint, self.udp_socket_ir.observer_endpoint)
            if endpoint
        ]

        factory_fn_args = [
            types.MOVE.invoke([types.CppValue(None, "memres")]),
            *endpoint_args,
            types.CppValue(_SOCKET_ENDPOINT_TYPE, {"host": types.CppValue(types.PMR_STRING, "host"), "port": "port"}),
        ]
        if is_bidirectional:
            factory_fn_args.extend(
                [
                    types.CppValue(
                        _SOCKET_ENDPOINT_TYPE,
                        {"host": types.CppValue(types.PMR_STRING, "remote_host"), "port": "remote_port"},
                    ),
                ]
            )
        elif self.udp_socket_ir.direction == udp.IODirection.incoming:
            assert isinstance(self.udp_socket_ir.batch_size, primitive.DecimalValue)
            factory_fn_args.append(types.CppValue(None, f"{self.udp_socket_ir.batch_size.value}UL"))

        if self.options:
            factory_fn_args.extend(self.options.render())

        factory_fn_call = types.CppFn(
            headers=[Header(JEWELS_REPO, "jewels/std/expected.hh")],
            namespace=socket_template,
            name="try_make",
        ).invoke(factory_fn_args)

        factory_fn.append(f"return {factory_fn_call.render(enclosing_namespace)};")
        factory_fn.context.add_includes(factory_fn_call.includes)

        socket_type.public.append(
            types.CppMethod(
                name="try_make",
                doc="Try to make the socket instance",
                return_type=types.AUTO,
                arguments=[types.CppNamedType(argument_type=types.MEMORY_RESOURCE, argument_name="memres")],
                leading_qualifiers=["inline"],
                trailing_qualifiers=[],
                body=factory_fn,
                no_discard=True,
                static=True,
            )
        )
        cpp_mod.append(socket_type.render(enclosing_namespace))
        return cpp_mod


@dataclass(eq=True, slots=True)
class CppAudioSource:
    """Instantiates an audio source inside cpp_target."""

    audio_source_ir: audio.AudioSource | expr.Expr

    @classmethod
    def from_cst(
        cls: type[CppAudioSource],
        cst_node: cst.CppAudioSource,
        module: node.Module,
    ) -> CppAudioSource:
        """Create an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        typespec = expr.Expr.from_cst(cst_node.child_typespec(), module)
        return cls(audio_source_ir=typespec)

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if not isinstance(self.audio_source_ir, expr.Expr):
            msg = f"Attempt to resolve CppAudioSource twice: {self}"
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        typespec = self.audio_source_ir.evaluate()
        if not isinstance(typespec, audio.AudioSource):
            msg = self.audio_source_ir.append_error_line(f"Expected AudioSource, got {type(typespec)}")
            raise TypeError(msg)
        self.audio_source_ir = typespec

    @classmethod
    def from_generate_cpp(cls: type[CppAudioSource], audio_source_ir: audio.AudioSource) -> CppAudioSource:
        """Create an IR node for an auto generated CppTarget."""
        return cls(audio_source_ir=audio_source_ir)

    def render(self, compiler_context: CompilerContext, enclosing_namespace: str) -> CppModuleChunks:
        """Convert the socket to C++."""
        if not isinstance(self.audio_source_ir, audio.AudioSource):
            msg = "Attempt to render before resolving."
            raise TypeError(msg)

        cpp_mod = CppModuleChunks()
        source_type = types.CppStruct(
            name=typereg.get_cpp_type(compiler_context, self.audio_source_ir),
            doc=self.audio_source_ir.doc.value,
        )
        uuid_type = typereg.get_cpp_template(compiler_context, clkbuiltins.UUID).instantiate(
            [
                types.CppType(
                    [Header(CLK_REPO, "clockwork/common/process_description_clk_cc.hh")],
                    "IoConnectionClassId",
                    typereg.CLOCKWORK_NAMESPACE + "::common",
                )
            ]
        )
        source_type.public.append(
            types.CppNamedValue(
                named_type=types.CppNamedType(
                    argument_type=uuid_type,
                    argument_name="uuid",
                ),
                value=values.uuid_to_byte_array(
                    self.audio_source_ir.module.context, uuid_reg.lookup_uuid(compiler_context, self.audio_source_ir)
                ),
                doc="Class type UUID.",
                qualifiers=["static", "constexpr"],
            )
        )

        if not isinstance(self.audio_source_ir.diagnostics.group_id, str):
            msg = self.audio_source_ir.append_error_line(
                f"Invalid group ID: {self.audio_source_ir.diagnostics.group_id}"
            )
            raise RuntimeError(msg)  # noqa: TRY004 (wrong group ID is a runtime error)
        if not isinstance(self.audio_source_ir.diagnostics.instance_id, str):
            msg = self.audio_source_ir.append_error_line(
                f"Invalid instance ID: {self.audio_source_ir.diagnostics.instance_id}"
            )
            raise RuntimeError(msg)  # noqa: TRY004 (wrong instance ID is a runtime error)
        diags_instance_id = diagnostics.to_instance_id(
            self.audio_source_ir.diagnostics.group_id, self.audio_source_ir.diagnostics.instance_id
        )

        factory_fn = CppChunk()
        source_template = audio.AUDIO_SOURCE_TEMPLATE.instantiate(
            [typereg.get_cpp_type(compiler_context, self.audio_source_ir.message_type)]
        )

        factory_fn_args = [
            types.MOVE.invoke([types.CppValue(None, "memres")]),
            construct_uuid_value(_ENDPOINT_CLASS_ID_TYPE, uuid_reg.lookup_uuid(compiler_context, self.audio_source_ir)),
            diags_instance_id,
        ]

        factory_fn_call = types.CppFn(
            headers=[Header(JEWELS_REPO, "jewels/std/expected.hh")],
            namespace=source_template,
            name="try_make",
        ).invoke(factory_fn_args)

        factory_fn.append(f"return {factory_fn_call.render(enclosing_namespace)};")
        factory_fn.context.add_includes(factory_fn_call.includes)

        source_type.public.append(
            types.CppMethod(
                name="try_make",
                doc="Try to make the audio source",
                return_type=types.AUTO,
                arguments=[types.CppNamedType(argument_type=types.MEMORY_RESOURCE, argument_name="memres")],
                leading_qualifiers=["inline"],
                trailing_qualifiers=[],
                body=factory_fn,
                no_discard=True,
                static=True,
            )
        )
        cpp_mod.append(source_type.render(enclosing_namespace))
        return cpp_mod


@dataclass
class CasingEntities:
    """Holds sets of casing entities."""

    externs: dict[str, ExternType] = field(default_factory=dict)
    representations: dict[schema_reg.RepresentationInfo, schema_reg.RepresentationInfo] = field(default_factory=dict)
    interfaces: dict[schema_reg.InterfaceInfo, schema_reg.InterfaceInfo] = field(default_factory=dict)
    cogs: dict[str, CppCog | CppInstantiatedCog] = field(default_factory=dict)
    python_cogs: dict[str, CppPythonCog] = field(default_factory=dict)
    udp_sockets: dict[str, CppUdpSocket] = field(default_factory=dict)
    audio_sources: dict[str, CppAudioSource] = field(default_factory=dict)

    def add_representation(
        self, compiler_context: CompilerContext, repr_typespec: typesys.Instantiation, error_node: node.CstNode[Any]
    ) -> None:
        """Add support for the given representation in the way the Casing requires.

        Note, this will often involve adding an interface rather than a
        representation, since that's how the C++ casing wants to get Tachyon
        representations (as Tappy interfaces).
        """
        if repr_typespec.instantiates is clkbuiltins.PROTOBUF:
            repr_ref = RepresentationReference.from_typespec(repr_typespec)
            if isinstance(repr_ref, str):
                msg = error_node.append_error_line(
                    f"Expected a schema representation: {repr_ref}",
                )
                raise TypeError(msg)
            repr_info = schema_reg.lookup_representation(compiler_context, repr_ref)
            if repr_info is None:
                msg = error_node.append_error_line(f"No representation registered for {repr_ref}")
                raise ValueError(msg)
            self.representations[repr_info] = repr_info
            return
        iface_ref = InterfaceReference.from_typespec(
            typesys.Instantiation(
                instantiates=clkbuiltins.TAP,
                arguments={"representation": repr_typespec},
                type_info=clkbuiltins.TYPE_TYPE,
            )
        )
        if isinstance(iface_ref, str):
            msg = error_node.append_error_line(iface_ref)
            raise ValueError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        iface_info = schema_reg.lookup_interface(compiler_context, iface_ref)
        if iface_info is None:
            msg = error_node.append_error_line(f"No interface registered for {iface_ref.typespec.value_key()}")
            raise ValueError(msg)
        self.interfaces[iface_info] = iface_info

    def merged_with(self, other: CasingEntities) -> CasingEntities:
        """Return a new set of entities merged with other."""
        return CasingEntities(
            externs=self.externs | other.externs,
            representations=self.representations | other.representations,
            interfaces=self.interfaces | other.interfaces,
            cogs=self.cogs | other.cogs,
            python_cogs=self.python_cogs | other.python_cogs,
            udp_sockets=self.udp_sockets | other.udp_sockets,
            audio_sources=self.audio_sources | other.audio_sources,
        )


class CasingEntitySource(ABC):
    """Base class for things that can produce a set of casing entities.

    This is necessary in particular to break a circular dependency between
    casings and boxes.  So boxes can implement this interface, which casing
    depends on, and boxes can depend directly on casings.
    """

    @abstractmethod
    def produce_casing_entities(self) -> CasingEntities:
        """Produce a set of casing entities."""


@dataclass(slots=True)
class CppExecutableCasingEntities:
    """Structure for entities passed to CppExecutable.from_generate_cpp_exe."""

    boxes: list[CasingEntitySource] = field(default_factory=list)


@dataclass
class Casing(node.CstNode[cst.Casing], node.DocableEntity):
    """IR Node representing a casing declaration."""

    externs: Sequence[ExternType] | Sequence[expr.Expr]
    representations: Sequence[schema_reg.RepresentationInfo] | Sequence[expr.Expr]
    interfaces: Sequence[schema_reg.InterfaceInfo] | Sequence[expr.Expr]
    cogs: Sequence[CppCog | CppInstantiatedCog]
    python_cogs: Sequence[CppPythonCog]
    udp_sockets: Sequence[CppUdpSocket]
    audio_sources: Sequence[CppAudioSource]
    boxes: Sequence[CasingEntitySource] | Sequence[expr.Expr]
    instantiations: Sequence[statement.InstantiateStmt]
    resolved: ResolvedCasing | None
    entities_are_resolved: bool

    # We must disable C901 here (function complexity) because
    # we inherently have many branches, one for each type of module-level entity.
    # However, they're handled in a uniform way that isn't difficult to understand.
    # We could in principle make a data-driven table of handlers instead of explicit
    # branches, but it would be awkward and would not decouple the code in a
    # meaningful way.
    @classmethod
    def from_cst(cls: type[Casing], cst_node: cst.Casing, module: node.Module) -> Casing:  # noqa: C901 (see above)
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        externs = []
        representations = []
        interfaces = []
        cogs = []
        python_cogs = []
        udp_sockets = []
        audio_sources = []
        boxes = []
        instantiations = []
        for element in cst_node.children_casing_element():
            if extern_cst := element.maybe_casing_extern():
                externs.append(expr.Expr.from_cst(extern_cst.child_typespec(), module))
            elif representation_cst := element.maybe_casing_representation():
                representations.append(expr.Expr.from_cst(representation_cst.child_typespec(), module))
            elif interface_cst := element.maybe_casing_interface():
                interfaces.append(expr.Expr.from_cst(interface_cst.child_typespec(), module))
            elif cog_cst := element.maybe_cpp_cog():
                cogs.append(CppCog.from_cst(cog_cst, module))
            elif python_cog_cst := element.maybe_cpp_python_cog():
                cpp_cog = CppCog.from_cst(python_cog_cst, module)
                cogs.append(cpp_cog)
                python_cogs.append(CppPythonCog(python_cog_cst, module, cpp_cog))
            elif udp_socket_cst := element.maybe_cpp_udp_socket():
                udp_socket_ir = CppUdpSocket.from_cst(udp_socket_cst, module)
                udp_sockets.append(udp_socket_ir)
            elif audio_source_cst := element.maybe_cpp_audio_source():
                audio_source_ir = CppAudioSource.from_cst(audio_source_cst, module)
                audio_sources.append(audio_source_ir)
            elif box_cst := element.maybe_casing_box():
                boxes.append(expr.Expr.from_cst(box_cst.child_typespec(), module))
            elif instantiation_cst := element.maybe_casing_instantiate_stmt():
                instantiation_ir = statement.InstantiateStmt.from_cst(instantiation_cst, module)
                instantiations.append(instantiation_ir)
            else:
                msg = node.append_error_line(element, module, "Unrecognized statement within casing")
                raise NotImplementedError(msg)
        return cls(
            doc=doc,
            module=module,
            cst_node=cst_node,
            externs=externs,
            representations=representations,
            interfaces=interfaces,
            cogs=cogs,
            python_cogs=python_cogs,
            udp_sockets=udp_sockets,
            audio_sources=audio_sources,
            boxes=boxes,
            instantiations=instantiations,
            resolved=None,
            entities_are_resolved=False,
        )

    def _resolve_entities(self) -> None:
        self.externs = [self._resolve_extern(extern) for extern in self.externs]
        self.representations = [self._resolve_representation(representation) for representation in self.representations]
        self.interfaces = [self._resolve_interface(interface) for interface in self.interfaces]
        for cpp_cog in self.cogs:
            assert isinstance(cpp_cog, CppCog)
            cpp_cog.resolve()
        for cpp_udp_socket in self.udp_sockets:
            cpp_udp_socket.resolve()
        for cpp_audio_source in self.audio_sources:
            cpp_audio_source.resolve()
        boxes: list[CasingEntitySource] = []
        for box in self.boxes:
            assert isinstance(box, expr.Expr)
            box_eval = box.evaluate()
            if not isinstance(box_eval, CasingEntitySource):
                msg = box.append_error_line(f"Expected a CasingEntitySource instance, got {type(box_eval)}")
                raise TypeError(msg)
            boxes.append(box_eval)
        for instantiate_stmt in self.instantiations:
            instantiate_stmt.resolve()
            if not isinstance(instantiate_stmt.instantiated, CasingEntitySource):
                msg = instantiate_stmt.append_error_line(
                    f"Expected a CasingEntitySource instance, got {type(instantiate_stmt.instantiated)}"
                )
                raise TypeError(msg)
            boxes.append(instantiate_stmt.instantiated)
        self.boxes = boxes
        self.instantiations = []
        self.entities_are_resolved = True

    def resolve(self) -> ResolvedCasing:
        """Perform finalization of the IR."""
        if self.resolved:
            return self.resolved
        if not self.entities_are_resolved:
            self._resolve_entities()
        externs: dict[str, ExternType] = {}
        for extern in self.externs:
            assert isinstance(extern, ExternType)
            externs[extern.value_key()] = extern
        representations: dict[schema_reg.RepresentationInfo, schema_reg.RepresentationInfo] = {}
        for representation in self.representations:
            assert isinstance(representation, schema_reg.RepresentationInfo)
            representations[representation] = representation
        interfaces: dict[schema_reg.InterfaceInfo, schema_reg.InterfaceInfo] = {}
        for interface in self.interfaces:
            assert isinstance(interface, schema_reg.InterfaceInfo)
            interfaces[interface] = interface
        entities = CasingEntities(
            externs=externs,
            representations=representations,
            interfaces=interfaces,
            cogs={cpp_cog.cog_ir.value_key(): cpp_cog for cpp_cog in self.cogs if isinstance(cpp_cog, CppCog)},
            python_cogs={
                cpp_python_cog.cpp_cog.cog_ir.value_key(): cpp_python_cog for cpp_python_cog in self.python_cogs
            },
            udp_sockets={udp.udp_socket_ir.value_key(): udp for udp in self.udp_sockets},
            audio_sources={source.audio_source_ir.value_key(): source for source in self.audio_sources},
        )
        for box in self.boxes:
            assert isinstance(box, CasingEntitySource)
            entities = entities.merged_with(box.produce_casing_entities())
        self.resolved = ResolvedCasing(doc=self.doc, module=self.module, cst_node=self.cst_node, entities=entities)
        return self.resolved

    @classmethod
    def from_generate_cpp_exe(cls: type[Casing], module: node.Module, entities: CppExecutableCasingEntities) -> Casing:
        """Generate an IR Casing from the entities defined in a module."""
        return cls(
            doc=module.doc,
            module=module,
            cst_node=None,
            externs=[],
            representations=[],
            interfaces=[],
            cogs=[],
            python_cogs=[],
            udp_sockets=[],
            audio_sources=[],
            boxes=entities.boxes,
            instantiations=[],
            resolved=None,
            entities_are_resolved=True,
        )

    def get_resolved(self) -> ResolvedCasing:
        """Get a resolved version of this object."""
        if not self.resolved:
            msg = "Attempt to access unresolved object"
            raise RuntimeError(msg)
        return self.resolved

    def _resolve_extern(self, typespec: ExternType | expr.Expr) -> ExternType:
        """Transform extern expressions into ExternTypes."""
        if isinstance(typespec, ExternType):
            msg = node.append_error_line(self.cst_node, self.module, f"Attempt to resolve extern twice: {typespec}")
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        eval_result = typespec.evaluate()
        if not isinstance(eval_result, ExternType):
            msg = typespec.append_error_line(
                f"Expected an extern type: {eval_result}",
            )
            raise TypeError(msg)
        return eval_result

    def _resolve_representation(
        self, typespec: schema_reg.RepresentationInfo | expr.Expr
    ) -> schema_reg.RepresentationInfo:
        """Transform representation expressions into RepresentationInfo."""
        if isinstance(typespec, schema_reg.RepresentationInfo):
            msg = node.append_error_line(
                self.cst_node, self.module, f"Attempt to resolve representation twice: {typespec}"
            )
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        eval_result = typespec.evaluate()
        representation_ref = RepresentationReference.from_typespec(eval_result)
        if isinstance(representation_ref, str):
            msg = typespec.append_error_line(
                f"Expected a schema representation: {representation_ref}",
            )
            raise TypeError(msg)
        representation = schema_reg.lookup_representation(self.module.context, representation_ref)
        if representation is None:
            msg = typespec.append_error_line(f"No representation registered for {representation_ref}")
            raise ValueError(msg)
        return representation

    def _resolve_interface(self, typespec: schema_reg.InterfaceInfo | expr.Expr) -> schema_reg.InterfaceInfo:
        """Transform interface expressions into Schema_reg.InterfaceInfos."""
        if isinstance(typespec, schema_reg.InterfaceInfo):
            msg = node.append_error_line(self.cst_node, self.module, f"Attempt to resolve interface twice: {typespec}")
            raise RuntimeError(msg)  # noqa: TRY004 (resolving twice is a runtime error)
        eval_result = typespec.evaluate()
        interface_ref = InterfaceReference.from_typespec(eval_result)
        if isinstance(interface_ref, str):
            msg = typespec.append_error_line(
                f"Expected a schema interface: {interface_ref}",
            )
            raise TypeError(msg)
        interface = schema_reg.lookup_interface(self.module.context, interface_ref)
        if interface is None:
            msg = typespec.append_error_line(
                dedent(
                    """
                    Interface not instantiated, or instantiation not visible.
                    Make sure this is instantiated in a cpp_target block in a module you've imported.
                    """,
                ),
            )
            raise TypeError(msg)
        return interface

    def get_py_deps(self) -> list[Label]:
        """Get the python dependencies from the casing."""
        resolved = self.get_resolved()
        py_deps: set[Label] = set()
        for python_cog in resolved.entities.python_cogs.values():
            py_deps.update(python_cog.get_py_deps())
        return sorted(py_deps)


@dataclass
class ResolvedCasing(node.CstNode[cst.Casing], node.DocableEntity):
    """IR Node representing a casing declaration."""

    entities: CasingEntities

    def render(self, full_includes: bool = False) -> CppModuleChunks:  # noqa: PLR0915 it's easier to follow the codegen with a single method.
        """Convert to C++."""
        # make_casing has to be in the scaffolding namespace
        cpp_namespace = "clockwork::scaffolding"

        mod = CppModuleChunks()
        # Only need to generate implementation file
        chunk = mod.implementation_chunk

        entities = self.entities

        extra_cpp_headers = []
        all_cpp_cog_types = []
        if full_includes:
            all_cpp_cog_types.extend(
                typereg.get_cpp_type(cpp_cog.cog_ir.module.context, cpp_cog.cog_ir)
                for cpp_cog in entities.cogs.values()
                if isinstance(cpp_cog, CppCog)
            )
            # Generic cog instantiations from boxes produce CppInstantiatedCog entries.
            # Their containing module's umbrella cc_library must be in the binary deps so
            # the factory-registration statics are linked in.  Resolve the instantiation
            # to its C++ type here so the umbrella header flows into the dep list.
            all_cpp_cog_types.extend(
                typereg.get_cpp_type(cpp_cog.instantiation.module.context, cpp_cog.instantiation.instantiation)
                for cpp_cog in entities.cogs.values()
                if isinstance(cpp_cog, CppInstantiatedCog)
            )

        all_cpp_schema_types: list[types.CppTypeExpr] = []
        for extern in entities.externs.values():
            cxx_schema_map = types.CXX_SCHEMA_SCHEMA.instantiate(
                [
                    typereg.get_cpp_type(self.module.context, extern),
                    values.uuid_to_value(
                        extern.module.context,
                        lookup_uuid(extern.module.context, extern),
                        tag=clkbuiltins.REPRESENTATION_TAG_TYPE,
                    ),
                ]
            )
            if full_includes:
                all_cpp_schema_types.append(cxx_schema_map)
        for representation in entities.representations:
            repr_ir = representation.representation_ir
            assert isinstance(repr_ir.typespec, typesys.TypeVal)
            if repr_ir.typespec.instantiates is not clkbuiltins.PROTOBUF:
                msg = f"casing only understands Protobuf<> representations, got {repr_ir.typespec}"
                raise TypeError(msg)
            schema = repr_ir.typespec.arguments["schema"]
            tachyon = typesys.Instantiation(
                instantiates=clkbuiltins.TAP,
                arguments={
                    "representation": typesys.Instantiation(
                        instantiates=clkbuiltins.TACHYON,
                        arguments={"schema": schema},
                        type_info=clkbuiltins.TYPE_TYPE,
                    )
                },
                type_info=clkbuiltins.TYPE_TYPE,
            )
            proto_tachyon_map = types.CASING_PROTO_SCHEMA.instantiate(
                [
                    protobuf_repr_to_cpp_type(representation, self.module.context),
                    values.uuid_to_value(
                        repr_ir.module.context,
                        lookup_uuid(repr_ir.module.context, repr_ir.typespec),
                        tag=clkbuiltins.REPRESENTATION_TAG_TYPE,
                    ),
                    typereg.get_cpp_type(repr_ir.module.context, tachyon),
                ]
            )
            all_cpp_schema_types.append(proto_tachyon_map)
            extra_cpp_headers.append(
                proto_to_tap.get_conversion_registration(schema, self.module.context).include_location
            )
        for interface in entities.interfaces:
            assert isinstance(interface.interface_ir.typespec, typesys.TypeVal)
            # Filter out schemas that are programmatically generated. We don't need those as template parameters to the casing.
            if (
                interface.interface_ir.representation
                and interface.interface_ir.representation.schema_ir.schema.source
                and interface.interface_ir.representation.schema_ir.schema.source.programmatically_generated
            ):
                continue
            # This must be included with full_includes because even if these don't need proto conv, they still may
            # include tachyon config which doesn't current have a factory registry.
            all_cpp_schema_types.append(
                typereg.get_cpp_type(interface.interface_ir.module.context, interface.interface_ir.typespec)
            )
        all_cpp_udp_socket_types = [
            typereg.get_cpp_type(self.module.context, udp_socket.udp_socket_ir)
            for udp_socket in entities.udp_sockets.values()
        ]

        all_cpp_audio_source_types = [
            typereg.get_cpp_type(audio_source.audio_source_ir.module.context, audio_source.audio_source_ir)
            for audio_source in entities.audio_sources.values()
        ]

        std_shared_ptr_t = types.CppTemplate([SystemHeader("memory")], "shared_ptr", "std")
        abstract_casing_t = types.CppType(
            [Header(CLK_REPO, "clockwork/scaffolding/abstract_casing.hh")],
            "AbstractCasing",
            "clockwork::scaffolding",
        )
        shared_abstract_casing = std_shared_ptr_t.instantiate([abstract_casing_t])
        chunk.context.add_includes(shared_abstract_casing.includes)

        memory_resource_t = types.CppNamedType(
            types.CppType(
                [Header(JEWELS_REPO, "jewels/memory/memory_resource.hh")], "MemoryResource", "jewels::memory"
            ),
            "memory_resource",
        )
        chunk.context.add_includes(memory_resource_t.includes)

        casing_impl_t = types.CppTemplate(
            [Header(CLK_REPO, "clockwork/scaffolding/casing.hh")],
            "CasingImpl",
            "clockwork::scaffolding",
        )
        tuple_t = types.CppTemplate([SystemHeader("tuple")], "tuple", "std")
        tuple_cogs = tuple_t.instantiate(all_cpp_cog_types)
        tuple_schemas = tuple_t.instantiate(all_cpp_schema_types)
        tuple_io_connections = tuple_t.instantiate(all_cpp_udp_socket_types + all_cpp_audio_source_types)
        casing = casing_impl_t.instantiate([tuple_cogs, tuple_schemas, tuple_io_connections])
        chunk.context.add_includes(casing.includes)
        chunk.context.add_includes(extra_cpp_headers)

        casing_alias = "Casing"
        make_pmr_shared_t = types.CppTemplate(
            [Header(JEWELS_REPO, "jewels/memory/pmr_shared_ptr.hh")], "make_pmr_shared", "jewels::memory"
        )
        make_pmr_shared_casing = make_pmr_shared_t.instantiate([types.CppType([], casing_alias, None)])
        chunk.context.add_includes(make_pmr_shared_casing.includes)

        chunk.append(
            [
                f"namespace {cpp_namespace}",
                "{",
            ]
        )
        chunk.append(
            [
                f"{shared_abstract_casing.render(cpp_namespace)} make_casing({memory_resource_t.render(cpp_namespace)})",
                "{",
            ]
        )
        chunk.append(f"using {casing_alias} = {casing.render(cpp_namespace)};", indent=1)
        chunk.append(
            f"return {make_pmr_shared_casing.render(cpp_namespace)}({memory_resource_t.argument_name}, {memory_resource_t.argument_name});",
            indent=1,
        )
        chunk.append("}")
        chunk.append(f"}} // namespace {cpp_namespace}")

        return mod


@dataclass
class CppExecutable(node.CstNode[cst.CppExecutable], node.DocableEntity, typesys.NamedValue):
    """IR Node representing a process declaration."""

    casing: Casing
    offline: bool | expr.Expr

    @classmethod
    def from_cst(cls: type[CppExecutable], cst_node: cst.CppExecutable, module: node.Module) -> CppExecutable:
        """Construct an IR node from a CST node."""
        if module.terminals is None:
            msg = "Cannot construct IR nodes from CST without a TerminalSource"
            raise ValueError(msg)
        doc = node.Doc.maybe_from_cst(cst_node.maybe_doc(), module)
        name = get_span(cst_node.child_identifier().child_value(), module.terminals)
        casing = Casing.from_cst(cst_node.child_casing(), module)
        offline: bool | expr.Expr = False
        for param in cst_node.children_cpp_executable_param():
            param_name = get_span(param.child_param().child_value(), module.terminals)
            value = expr.Expr.from_cst(param.child_value(), module)
            if param_name == "offline":
                typesys.unify(clkbuiltins.BOOL, value.type_info)
                offline = value
            else:
                msg = f"Unsupported cpp_executable parameter '{param_name}'"
                raise NotImplementedError(msg)
        return CppExecutable(
            name=name,
            scope=module.inner_scope,
            type_info=clkbuiltins.EXECUTABLE_TYPE,
            doc=doc,
            module=module,
            cst_node=cst_node,
            casing=casing,
            offline=offline,
        )

    def resolve(self) -> None:
        """Perform finalization of the IR."""
        if isinstance(self.offline, expr.Expr):
            result = self.offline.evaluate()
            if not isinstance(result, typesys.NamedValue):
                msg = self.offline.append_error_line(
                    f"Expected a NamedValue (true or false) for parameter offline, but got {type(result)}",
                )
                raise TypeError(msg)
            self.offline = primitive.value_to_bool(result)
        self.casing.resolve()

    @classmethod
    def from_generate_cpp_exe(
        cls: type[CppExecutable], module: node.Module, entities: CppExecutableCasingEntities
    ) -> CppExecutable:
        """Generate an IR CppExecutable from the entities defined in a module."""
        if not entities.boxes:
            msg = node.append_error_line(
                module.cst_node, module, "At least one box is required when generating clk_exe"
            )
            raise ValueError(msg)
        assert module.inner_attrs is not None
        return cls(
            doc=module.doc,
            scope=module.inner_scope,
            type_info=clkbuiltins.EXECUTABLE_TYPE,
            name=f"{module.module_id.name.split('::')[-1]}_clk_exe",
            module=module,
            cst_node=None,
            casing=Casing.from_generate_cpp_exe(module, entities),
            offline=module.inner_attrs.get_exe_offline(),
        )

    @classmethod
    def from_generate_py_exe(
        cls: type[CppExecutable], module: node.Module, entities: CppExecutableCasingEntities
    ) -> CppExecutable:
        """Generate an IR CppExecutable from the entities defined in a module."""
        if not entities.boxes:
            msg = node.append_error_line(module.cst_node, module, "At least one box is required when generating py_exe")
            raise ValueError(msg)
        assert module.inner_attrs is not None
        return cls(
            doc=module.doc,
            scope=module.inner_scope,
            type_info=clkbuiltins.EXECUTABLE_TYPE,
            name=f"{module.module_id.name.split('::')[-1]}_clk_exe",
            module=module,
            cst_node=None,
            casing=Casing.from_generate_cpp_exe(module, entities),
            offline=module.inner_attrs.get_exe_offline(),
        )

    def render_and_write(self, root_dir: Path) -> None:
        """Render and write to a file."""
        exe_mod = self.casing.get_resolved().render()

        write_dir = root_dir / BazelPathResolver().to_buildtime_path(self.module.module_id).parent
        include_dir = self.module.module_id.get_base_path().parent

        write_to_file(
            rendered_cpp_mod=exe_mod,
            write_dir=write_dir,
            include_dir=include_dir,
            stem=self.name,
            current_repo=self.module.module_id.repo,
        )

    def output_targets(self) -> list[CcBinary | CcBinaryWithEmbeddedPy]:
        """Extract language target dependency information."""
        exe_mod = self.casing.get_resolved().render(full_includes=True)
        include_dir = self.module.module_id.get_base_path().parent
        py_deps = self.casing.get_py_deps()
        online_main_label = get_bazel_label_for_clk_label(
            self.module.module_id.repo, "//clockwork/scaffolding:online_main"
        )
        python_online_main_label = get_bazel_label_for_clk_label(
            self.module.module_id.repo, "//clockwork/scaffolding:python_online_main"
        )
        offline_main_label = get_bazel_label_for_clk_label(
            self.module.module_id.repo, "//clockwork/scaffolding:offline_main"
        )
        python_offline_main_label = get_bazel_label_for_clk_label(
            self.module.module_id.repo, "//clockwork/scaffolding:python_offline_main"
        )
        if py_deps:
            binary_with_py = as_cc_binary_with_embedded_py(
                exe_mod,
                self.name,
                include_dir,
                py_deps,
                self.module.module_id,
            )
            binary_with_py.deps = list(binary_with_py.deps)
            if self.offline:
                binary_with_py.deps.append(python_offline_main_label)
            else:
                binary_with_py.deps.append(python_online_main_label)
            return [binary_with_py]
        binary = as_cc_binary(exe_mod, self.name, include_dir, self.module.module_id)
        binary.deps = list(binary.deps)
        if self.offline:
            binary.deps.append(offline_main_label)
        else:
            binary.deps.append(online_main_label)
        return [binary]

    def has_embedded_python(self) -> bool:
        """Test whether this executable has embedded python."""
        return bool(self.casing.get_py_deps())
