# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ cog test wrappers."""

from dataclasses import dataclass, field

from clockwork.dsl.cog.codegen_helpers import (
    get_rendered_cog_instantiation_args,
    get_template_args,
    get_template_params,
    to_camel,
)
from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, Header, SystemHeader
from clockwork.dsl.cpp.types import (
    BOOLEAN,
    MEMORY_RESOURCE,
    MICROSECONDS,
    STRING_VIEW,
    SYNC_TIME,
    VOID,
    CppConstructor,
    CppMethod,
    CppNamedType,
    CppNamedValue,
    CppStruct,
    CppTemplateParam,
    CppTemplateType,
    CppType,
    CppTypeAliasDef,
    CppTypeExpr,
    CppValue,
    CppValueExpr,
    Ref,
    ref_qualify,
)
from clockwork.dsl.ir import (
    aligner as aligner_ir,
)
from clockwork.dsl.ir import (
    cog,
    cog_components,
)
from clockwork.dsl.ir.module_id import CLK_REPO


@dataclass
class CppTestCog:
    """Representation of the C++ implementation of a C++ test cog."""

    cog_ir: cog.Cog
    cpp_namespace: str | None
    cog_header: Header
    generate_combo_test: bool = False
    template_params: list[CppTemplateParam] = field(default_factory=list)
    template_args: list[CppTypeExpr | CppValueExpr] = field(default_factory=list)
    policy_class_name: str = field(default_factory=str)
    template_prefix: str = ""
    typename_prefix: str = ""

    def _render_memory_resources(self, wrapper: CppStruct) -> None:
        """Render the C++ code for the cog memory resources for the generated test wrapper.

        Arguments:
            wrapper: Generated test wrapper.
        """
        if not self.cog_ir.resources:
            return

        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapperMemoryResources", None),
            doc=f"{self.cog_ir.name}TestWrapperMemoryResources",
        )

        struct.public.append(
            CppTypeAliasDef(
                name="MemoryResourcesType",
                alias_for=CppType(
                    [],
                    f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::MemoryResourcesType",
                    None,
                ),
                doc=None,
            )
        )

        memory_resources_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="UnitTestCogMemoryResources<MemoryResourcesType>",
            cpp_namespace="clockwork::testing",
        )

        struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=memory_resources_type,
                    argument_name="memory_resources_",
                ),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[CppNamedType(memory_resources_type, "memory_resources")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[("memory_resources_", "::std::move(memory_resources)")],
                body=CppChunk(),
            )
        )

        for index, config in enumerate(self.cog_ir.resources.values()):
            get_body = CppChunk()
            get_body.append(f"return memory_resources_.{self.template_prefix}get_memory_resource<{index}>();")
            get_method = CppMethod(
                name=f"get_{config.name}",
                doc=None,
                return_type=CppType(
                    [],
                    f"const typename ::std::tuple_element_t<{index}, {self.typename_prefix}MemoryResourcesType::PoliciesTuple>::MemoryResourceType&",
                    None,
                ),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=get_body,
                no_discard=True,
            )
            struct.public.append(get_method)

            set_body = CppChunk()
            set_body.append(
                f"memory_resources_.{self.template_prefix}set_memory_resource<{index}>(::std::move(memory_resource));"
            )
            set_method = CppMethod(
                name=f"set_{config.name}",
                doc=None,
                return_type=VOID,
                arguments=[
                    CppNamedType(
                        CppType(
                            [],
                            f"typename ::std::tuple_element_t<{index}, {self.typename_prefix}MemoryResourcesType::PoliciesTuple>::MemoryResourceType",
                            None,
                        ),
                        "memory_resource",
                    )
                ],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=set_body,
                no_discard=False,
            )
            struct.public.append(set_method)

        wrapper.public.append(struct)

        memory_resources_body = CppChunk()
        memory_resources_body.append(
            f"return {self.cog_ir.name}TestWrapperMemoryResources{{unit_test_cog_.get_memory_resources()}};"
        )
        memory_resources_method = CppMethod(
            name="get_resources",
            doc=None,
            return_type=CppType(
                [], f"{self.cog_ir.name}TestWrapper::{self.cog_ir.name}TestWrapperMemoryResources", None
            ),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=memory_resources_body,
            no_discard=True,
        )
        wrapper.public.append(memory_resources_method)

    def _render_configs(self, wrapper: CppStruct) -> None:
        """Render the C++ code for the cog configs for the generated test wrapper.

        Arguments:
            wrapper: Generated test wrapper.
        """
        if not self.cog_ir.configs:
            return

        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapperConfigs", None), doc=f"{self.cog_ir.name}TestWrapperConfigs"
        )

        struct.public.append(
            CppTypeAliasDef(
                name="ConfigsType",
                alias_for=CppType(
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::ConfigsType", None
                ),
                doc=None,
            )
        )

        configs_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="UnitTestCogConfigs<ConfigsType>",
            cpp_namespace="clockwork::testing",
        )

        struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=configs_type,
                    argument_name="configs_",
                ),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[CppNamedType(configs_type, "configs")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[("configs_", "::std::move(configs)")],
                body=CppChunk(),
            )
        )

        for index, config in enumerate(self.cog_ir.configs.values()):
            get_body = CppChunk()
            get_body.append(f"return configs_.{self.template_prefix}get_config<{index}>();")
            get_method = CppMethod(
                name=f"get_{config.name}",
                doc=None,
                return_type=CppType([], f"typename ConfigsType::template ConfigType<{index}>&", None),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=get_body,
                no_discard=True,
            )
            struct.public.append(get_method)

            get_handle_body = CppChunk()
            get_handle_body.append(f"return configs_.{self.template_prefix}get_config_handle<{index}>();")
            get_handle_method = CppMethod(
                name=f"get_{config.name}_handle",
                doc=None,
                return_type=CppType(
                    [],
                    f"typename ConfigsType::template ConfigHandleType<{index}>",
                    None,
                ),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=["const"],
                body=get_handle_body,
                no_discard=True,
            )
            struct.public.append(get_handle_method)

            set_handle_body = CppChunk()
            set_handle_body.append(
                f"configs_.{self.template_prefix}set_config_handle<{index}>(::std::move(config_handle));"
            )
            set_handle_method = CppMethod(
                name=f"set_{config.name}_handle",
                doc=None,
                return_type=VOID,
                arguments=[
                    CppNamedType(
                        CppType(
                            [],
                            f"typename ConfigsType::template ConfigHandleType<{index}>",
                            None,
                        ),
                        "config_handle",
                    )
                ],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=set_handle_body,
                no_discard=False,
            )
            struct.public.append(set_handle_method)

        wrapper.public.append(struct)

        configs_body = CppChunk()
        configs_body.append(f"return {self.cog_ir.name}TestWrapperConfigs{{unit_test_cog_.get_configs()}};")
        configs_method = CppMethod(
            name="get_configs",
            doc=None,
            return_type=CppType([], f"{self.cog_ir.name}TestWrapper::{self.cog_ir.name}TestWrapperConfigs", None),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=configs_body,
            no_discard=True,
        )
        wrapper.public.append(configs_method)

    def _render_states(self, wrapper: CppStruct) -> None:
        """Render the C++ code for the cog states for the generated test wrapper.

        Arguments:
            wrapper: Generated test wrapper.
        """
        if not self.cog_ir.states:
            return

        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapperStates", None), doc=f"{self.cog_ir.name}TestWrapperStates"
        )

        struct.public.append(
            CppTypeAliasDef(
                name="StatesType",
                alias_for=CppType(
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::StatesType", None
                ),
                doc=None,
            )
        )

        states_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="UnitTestCogStates<StatesType>",
            cpp_namespace="clockwork::testing",
        )

        struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=states_type,
                    argument_name="states_",
                ),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[CppNamedType(states_type, "states")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[("states_", "::std::move(states)")],
                body=CppChunk(),
            )
        )

        for index, state in enumerate(self.cog_ir.states.values()):
            get_body = CppChunk()
            get_body.append(f"return states_.{self.template_prefix}get_state<{index}>();")
            get_method = CppMethod(
                name=f"get_{state.name}",
                doc=None,
                return_type=CppType([], f"typename StatesType::template SchemaType<{index}>&", None),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=get_body,
                no_discard=True,
            )
            struct.public.append(get_method)

            get_handle_body = CppChunk()
            get_handle_body.append(f"return states_.{self.template_prefix}get_state_handle<{index}>();")
            get_handle_method = CppMethod(
                name=f"get_{state.name}_handle",
                doc=None,
                return_type=CppType([], f"typename StatesType::template RecordPtrType<{index}>", None),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=["const"],
                body=get_handle_body,
                no_discard=True,
            )
            struct.public.append(get_handle_method)

            set_handle_body = CppChunk()
            set_handle_body.append(
                f"states_.{self.template_prefix}set_state_handle<{index}>(::std::move(state_handle));"
            )
            set_handle_method = CppMethod(
                name=f"set_{state.name}_handle",
                doc=None,
                return_type=VOID,
                arguments=[
                    CppNamedType(
                        CppType(
                            [],
                            f"typename StatesType::template RecordPtrType<{index}>",
                            None,
                        ),
                        "state_handle",
                    )
                ],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=set_handle_body,
                no_discard=False,
            )
            struct.public.append(set_handle_method)

        wrapper.public.append(struct)

        states_body = CppChunk()
        states_body.append(f"return {self.cog_ir.name}TestWrapperStates{{unit_test_cog_.get_states()}};")
        states_method = CppMethod(
            name="get_states",
            doc=None,
            return_type=CppType([], f"{self.cog_ir.name}TestWrapper::{self.cog_ir.name}TestWrapperStates", None),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=states_body,
            no_discard=True,
        )
        wrapper.public.append(states_method)

    def _render_inputs(self, wrapper: CppStruct) -> None:
        """Render the C++ code for the cog inputs for the generated test wrapper.

        Arguments:
            wrapper: Generated test wrapper.
        """
        dial_inputs = []
        for input_def in self.cog_ir.inputs.values():
            if input_def.view_params.no_dial:
                continue
            if input_def.elements:
                dial_inputs.extend(input_def.elements)
            else:
                # pyrefly: ignore[bad-argument-type] # TODO(DX-3792): Address pyrefly errors ignored to migrate from pyright
                dial_inputs.append(input_def)
        if not dial_inputs:
            return

        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapperInputs", None), doc=f"{self.cog_ir.name}TestWrapperInputs"
        )

        struct.public.append(
            CppTypeAliasDef(
                name="InputsType",
                alias_for=CppType(
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::InputsType", None
                ),
                doc=None,
            )
        )

        inputs_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="UnitTestCogInputs<InputsType>",
            cpp_namespace="clockwork::testing",
        )

        struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=inputs_type,
                    argument_name="inputs_",
                ),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[CppNamedType(inputs_type, "inputs")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[("inputs_", "::std::move(inputs)")],
                body=CppChunk(),
            )
        )

        for index, cog_input in enumerate(dial_inputs):
            if isinstance(cog_input, cog.InputDef):
                get_body = CppChunk()
                get_body.append(f"return inputs_.{self.template_prefix}get_input<{index}>();")
                get_method = CppMethod(
                    name=f"get_{cog_input.name}",
                    doc=None,
                    return_type=CppType(
                        includes=[],
                        type_name=f"UnitTestCogInput<typename InputsType::template PolicyType<{index}>>",
                        cpp_namespace="clockwork::testing",
                    ),
                    arguments=[],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=get_body,
                    no_discard=True,
                )
                struct.public.append(get_method)
            elif isinstance(cog_input, cog_components.InputDefElement) and cog_input.index == 0:
                assert isinstance(cog_input.view_params.multi_connect, int)
                get_body = CppChunk()
                get_values = ", ".join(
                    f"::clockwork::testing::UnitTestCogInput<typename InputsType::template PolicyType<{index}>>{{inputs_.{self.template_prefix}get_input<{index + element_index}>()}}"
                    for element_index in range(cog_input.view_params.multi_connect)
                )
                get_body.append(f"return {{{get_values}}};")
                return_type = CppType(
                    includes=[],
                    type_name=f"UnitTestCogInput<typename InputsType::template PolicyType<{index}>>",
                    cpp_namespace="clockwork::testing",
                )
                return_type = CppTemplateType(
                    include=[SystemHeader("array")],
                    template_name="array",
                    cpp_namespace="std",
                    arguments=[return_type, CppValue(None, f"{cog_input.view_params.multi_connect}")],
                )
                get_method = CppMethod(
                    name=f"get_{cog_input.base_name}",
                    doc=None,
                    return_type=return_type,
                    arguments=[],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=get_body,
                    no_discard=True,
                )
                struct.public.append(get_method)

        wrapper.public.append(struct)

        inputs_body = CppChunk()
        inputs_body.append(f"return {self.cog_ir.name}TestWrapperInputs{{unit_test_cog_.get_inputs()}};")
        inputs_method = CppMethod(
            name="get_inputs",
            doc=None,
            return_type=CppType([], f"{self.cog_ir.name}TestWrapper::{self.cog_ir.name}TestWrapperInputs", None),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=inputs_body,
            no_discard=True,
        )
        wrapper.public.append(inputs_method)

    def _render_aligned_inputs(self, wrapper: CppStruct) -> None:
        """Render the C++ code for aligned input groups for the generated test wrapper.

        For each aligned input group, generates:
        - A group struct with named ``publish_*()`` methods for each upstream channel
          and the alignment message, plus an ``AlignedBatch`` builder class.
        - A ``get_{group_name}()`` accessor on the wrapper returning the group struct.

        Arguments:
            wrapper: Generated test wrapper.
        """
        if not self.cog_ir.aligned_inputs:
            return

        regular_count = sum(1 for idef in self.cog_ir.inputs.values() if not idef.view_params.no_dial)

        policy_index = regular_count

        for group_name, aligned_def in self.cog_ir.aligned_inputs.items():
            aligner_type = aligned_def.aligned_type
            if not isinstance(aligner_type, aligner_ir.Aligner):
                msg = f"Expected Aligner, got {type(aligner_type).__name__}"
                raise TypeError(msg)
            resolved = aligner_type.get_resolved()

            alignment_msg_index = policy_index
            policy_index += 1

            upstream_indices: dict[str, int] = {}
            for input_name in resolved.inputs:
                upstream_indices[input_name] = policy_index
                policy_index += 1

            self._render_aligned_group(
                wrapper,
                group_name,
                alignment_msg_index,
                upstream_indices,
                resolved,
            )

    def _render_aligned_group(
        self,
        wrapper: CppStruct,
        group_name: str,
        alignment_msg_index: int,
        upstream_indices: dict[str, int],
        resolved: aligner_ir.ResolvedAligner,
    ) -> None:
        """Render a single aligned input group and its builder.

        Arguments:
            wrapper: Top-level test wrapper struct.
            group_name: Name of the aligned group (e.g. "aligned").
            alignment_msg_index: Policy index for the alignment message.
            upstream_indices: Map of input name to policy index.
            resolved: Resolved aligner definition.
        """
        group_struct_name = f"{self.cog_ir.name}TestWrapper{to_camel(group_name)}Inputs"

        group_struct = CppStruct(
            name=CppType([], group_struct_name, None),
            doc=group_struct_name,
        )

        group_struct.public.append(
            CppTypeAliasDef(
                name="InputsType",
                alias_for=CppType(
                    [],
                    f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::InputsType",
                    None,
                ),
                doc=None,
            )
        )

        inputs_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="UnitTestCogInputs<InputsType>",
            cpp_namespace="clockwork::testing",
        )

        group_struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=inputs_type,
                    argument_name="inputs_",
                ),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        group_struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[CppNamedType(inputs_type, "inputs")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[("inputs_", "::std::move(inputs)")],
                body=CppChunk(),
            )
        )

        publish_align_body = CppChunk()
        publish_align_body.append(
            f"return inputs_.{self.template_prefix}get_input<{alignment_msg_index}>().publish(init_fn, publish_time);"
        )
        group_struct.public.append(
            CppMethod(
                name="publish_alignment_msg",
                doc=None,
                return_type=CppType(
                    includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
                    type_name="MessageHandle",
                    cpp_namespace="clockwork::testing",
                ),
                arguments=[
                    CppNamedType(
                        CppType(
                            includes=[SystemHeader("functional")],
                            type_name=f"std::function<void(typename InputsType::template PolicyType<{alignment_msg_index}>::MsgType&)>",
                            cpp_namespace=None,
                            const=True,
                            ref=Ref.L,
                        ),
                        "init_fn",
                    ),
                    CppNamedType(SYNC_TIME, "publish_time"),
                ],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=publish_align_body,
                no_discard=True,
            )
        )

        for input_name, upstream_index in upstream_indices.items():
            publish_body = CppChunk()
            publish_body.append(
                f"return inputs_.{self.template_prefix}get_input<{upstream_index}>().publish(init_fn, publish_time);"
            )
            group_struct.public.append(
                CppMethod(
                    name=f"publish_{input_name}",
                    doc=None,
                    return_type=CppType(
                        includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
                        type_name="MessageHandle",
                        cpp_namespace="clockwork::testing",
                    ),
                    arguments=[
                        CppNamedType(
                            CppType(
                                includes=[SystemHeader("functional")],
                                type_name=f"std::function<void(typename InputsType::template PolicyType<{upstream_index}>::MsgType&)>",
                                cpp_namespace=None,
                                const=True,
                                ref=Ref.L,
                            ),
                            "init_fn",
                        ),
                        CppNamedType(SYNC_TIME, "publish_time"),
                    ],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=publish_body,
                    no_discard=False,
                )
            )

        batch_struct = self._render_aligned_batch(
            group_struct_name,
            resolved,
        )
        group_struct.public.append(batch_struct)

        make_batch_body = CppChunk()
        make_batch_body.append("return AlignedBatch{*this};")
        group_struct.public.append(
            CppMethod(
                name="make_batch",
                doc=None,
                return_type=CppType([], f"{group_struct_name}::AlignedBatch", None),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=make_batch_body,
                no_discard=True,
            )
        )

        wrapper.public.append(group_struct)

        accessor_body = CppChunk()
        accessor_body.append(f"return {group_struct_name}{{unit_test_cog_.get_inputs()}};")
        wrapper.public.append(
            CppMethod(
                name=f"get_{group_name}",
                doc=None,
                return_type=CppType(
                    [],
                    f"{self.cog_ir.name}TestWrapper::{group_struct_name}",
                    None,
                ),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=accessor_body,
                no_discard=True,
            )
        )

    def _render_aligned_batch(
        self,
        group_struct_name: str,
        resolved: aligner_ir.ResolvedAligner,
    ) -> CppStruct:
        """Render the AlignedBatch builder class for an aligned input group.

        Arguments:
            group_struct_name: Name of the enclosing group struct.
            resolved: Resolved aligner definition.

        Returns:
            CppStruct for the AlignedBatch builder.
        """
        message_handle_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="MessageHandle",
            cpp_namespace="clockwork::testing",
        )
        bool_type = CppType([], "bool", None)

        batch_struct = CppStruct(
            name=CppType([], "AlignedBatch", None),
            doc="AlignedBatch",
        )

        batch_struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[
                    CppNamedType(
                        ref_qualify(CppType([], group_struct_name, None), Ref.L),
                        "parent",
                    ),
                ],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[("parent_", "parent")],
                body=CppChunk(),
            )
        )

        batch_struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(
                    ref_qualify(CppType([], group_struct_name, None), Ref.L),
                    "parent_",
                ),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        for input_name, resolved_input in resolved.inputs.items():
            is_batch = resolved_input.batch_size is not None
            is_optional = resolved_input.optional

            set_flag_line = f"has_{input_name}_ = true;" if is_optional else f"{input_name}_set_ = true;"

            if is_batch:
                # set_{name}_range(begin, end_inclusive)
                setter_body = CppChunk()
                setter_body.append(
                    [
                        f"{input_name}_begin_handle_ = begin;",
                        f"{input_name}_end_inclusive_handle_ = end_inclusive;",
                        set_flag_line,
                    ]
                )
                batch_struct.public.append(
                    CppMethod(
                        name=f"set_{input_name}_range",
                        doc=None,
                        return_type=VOID,
                        arguments=[
                            CppNamedType(message_handle_type, "begin"),
                            CppNamedType(message_handle_type, "end_inclusive"),
                        ],
                        leading_qualifiers=[],
                        trailing_qualifiers=[],
                        body=setter_body,
                        no_discard=False,
                    )
                )

                batch_struct.private.append(
                    CppNamedValue(
                        named_type=CppNamedType(message_handle_type, f"{input_name}_begin_handle_"),
                        value=None,
                        doc=None,
                        qualifiers=None,
                        render_initializer=True,
                    )
                )
                batch_struct.private.append(
                    CppNamedValue(
                        named_type=CppNamedType(message_handle_type, f"{input_name}_end_inclusive_handle_"),
                        value=None,
                        doc=None,
                        qualifiers=None,
                        render_initializer=True,
                    )
                )
            else:
                # set_{name}(handle)
                setter_body = CppChunk()
                setter_body.append(
                    [
                        f"{input_name}_handle_ = handle;",
                        set_flag_line,
                    ]
                )
                batch_struct.public.append(
                    CppMethod(
                        name=f"set_{input_name}",
                        doc=None,
                        return_type=VOID,
                        arguments=[
                            CppNamedType(message_handle_type, "handle"),
                        ],
                        leading_qualifiers=[],
                        trailing_qualifiers=[],
                        body=setter_body,
                        no_discard=False,
                    )
                )

                batch_struct.private.append(
                    CppNamedValue(
                        named_type=CppNamedType(message_handle_type, f"{input_name}_handle_"),
                        value=None,
                        doc=None,
                        qualifiers=None,
                        render_initializer=True,
                    )
                )

            if is_optional:
                unset_body = CppChunk()
                unset_body.append(f"has_{input_name}_ = false;")
                batch_struct.public.append(
                    CppMethod(
                        name=f"unset_{input_name}",
                        doc=None,
                        return_type=VOID,
                        arguments=[],
                        leading_qualifiers=[],
                        trailing_qualifiers=[],
                        body=unset_body,
                        no_discard=False,
                    )
                )

                batch_struct.private.append(
                    CppNamedValue(
                        named_type=CppNamedType(bool_type, f"has_{input_name}_"),
                        value=CppValue(None, "false"),
                        doc=None,
                        qualifiers=None,
                        render_initializer=True,
                    )
                )
            else:
                batch_struct.private.append(
                    CppNamedValue(
                        named_type=CppNamedType(bool_type, f"{input_name}_set_"),
                        value=CppValue(None, "false"),
                        doc=None,
                        qualifiers=None,
                        render_initializer=True,
                    )
                )

        publish_body = self._render_batch_publish_body(resolved)

        batch_struct.public.append(
            CppMethod(
                name="publish",
                doc=None,
                return_type=CppType(
                    includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
                    type_name="MessageHandle",
                    cpp_namespace="clockwork::testing",
                ),
                arguments=[
                    CppNamedType(SYNC_TIME, "publish_time"),
                ],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=publish_body,
                no_discard=True,
            )
        )

        return batch_struct

    def _render_batch_publish_body(
        self,
        resolved: aligner_ir.ResolvedAligner,
    ) -> CppChunk:
        """Render the body of the AlignedBatch::publish() method.

        Arguments:
            resolved: Resolved aligner definition.

        Returns:
            CppChunk containing the publish method body.
        """
        publish_body = CppChunk()

        # Validation: check all required (non-optional) inputs are set
        for input_name, resolved_input in resolved.inputs.items():
            if not resolved_input.optional:
                is_batch = resolved_input.batch_size is not None
                field_desc = f"{input_name} range" if is_batch else input_name
                publish_body.append(
                    [
                        f'if (!{input_name}_set_) {{ throw ::std::runtime_error("{field_desc} must be set before publish"); }}',
                    ]
                )

        publish_body.append("return parent_.publish_alignment_msg(")
        publish_body.append("  [this](auto& alignment_msg) {")

        for input_name, resolved_input in resolved.inputs.items():
            is_batch = resolved_input.batch_size is not None
            is_optional = resolved_input.optional

            if is_optional and not is_batch:
                publish_body.append(f"    alignment_msg.set_has_{input_name}(has_{input_name}_);")
                publish_body.append(f"    if (has_{input_name}_) {{")
                publish_body.append(f"      alignment_msg.set_{input_name}_seq({input_name}_handle_.seqno);")
                publish_body.append("    }")
            elif is_optional and is_batch:
                publish_body.append(f"    alignment_msg.set_has_{input_name}(has_{input_name}_);")
                publish_body.append(f"    if (has_{input_name}_) {{")
                publish_body.append(
                    f"      alignment_msg.set_{input_name}_begin_seq({input_name}_begin_handle_.seqno);"
                )
                publish_body.append(
                    f"      alignment_msg.set_{input_name}_end_seq({input_name}_end_inclusive_handle_.seqno);"
                )
                publish_body.append("    }")
            elif is_batch:
                publish_body.append(f"    alignment_msg.set_{input_name}_begin_seq({input_name}_begin_handle_.seqno);")
                publish_body.append(
                    f"    alignment_msg.set_{input_name}_end_seq({input_name}_end_inclusive_handle_.seqno);"
                )
            else:
                publish_body.append(f"    alignment_msg.set_{input_name}_seq({input_name}_handle_.seqno);")

        publish_body.append("  },")
        publish_body.append("  publish_time);")

        return publish_body

    def _render_outputs(self, wrapper: CppStruct) -> None:
        """Render the C++ code for the cog outputs for the generated test wrapper.

        Arguments:
            wrapper: Generated test wrapper.
        """
        if not self.cog_ir.outputs:
            return

        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapperOutputs", None), doc=f"{self.cog_ir.name}TestWrapperOutputs"
        )

        struct.public.append(
            CppTypeAliasDef(
                name="PublishersType",
                alias_for=CppType(
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::PublishersType", None
                ),
                doc=None,
            )
        )

        outputs_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="UnitTestCogOutputs<PublishersType>",
            cpp_namespace="clockwork::testing",
        )

        struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(
                    argument_type=outputs_type,
                    argument_name="outputs_",
                ),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[CppNamedType(outputs_type, "outputs")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[("outputs_", "::std::move(outputs)")],
                body=CppChunk(),
            )
        )

        for index, cog_output in enumerate(self.cog_ir.outputs.values()):
            get_body = CppChunk()
            get_body.append(f"return outputs_.{self.template_prefix}get_output<{index}>();")
            get_method = CppMethod(
                name=f"get_{cog_output.name}",
                doc=None,
                return_type=CppType(
                    includes=[],
                    type_name=f"UnitTestCogOutput<typename PublishersType::template UnitTestOutputViewPolicyType<{index}>>",
                    cpp_namespace="clockwork::testing",
                ),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=get_body,
                no_discard=True,
            )
            struct.public.append(get_method)

        wrapper.public.append(struct)

        outputs_body = CppChunk()
        outputs_body.append(f"return {self.cog_ir.name}TestWrapperOutputs{{unit_test_cog_.get_outputs()}};")
        outputs_method = CppMethod(
            name="get_outputs",
            doc=None,
            return_type=CppType([], f"{self.cog_ir.name}TestWrapper::{self.cog_ir.name}TestWrapperOutputs", None),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=outputs_body,
            no_discard=True,
        )
        wrapper.public.append(outputs_method)

    def _render_signals(self, wrapper: CppStruct) -> None:
        """Render the C++ code for the cog signals for the generated test wrapper.

        Arguments:
            wrapper: Generated test wrapper.
        """
        if not self.cog_ir.report_groups:
            return

        signals_offset = (
            len(self.cog_ir.outputs) + len(self.cog_ir.metrics_outputs) + len(self.cog_ir.cog_metrics_report_groups)
        )

        if len(self.cog_ir.report_groups) == 1:
            wrapper.public.append(
                CppTypeAliasDef(
                    name="PublishersType",
                    alias_for=CppType(
                        [],
                        f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::PublishersType",
                        None,
                    ),
                    doc=None,
                )
            )

            signals_body = CppChunk()
            signals_body.append(
                f"return unit_test_cog_.get_outputs().{self.template_prefix}get_output<{signals_offset}>();"
            )
            signals_method = CppMethod(
                name="get_signals",
                doc=None,
                return_type=CppType(
                    includes=[],
                    type_name=f"UnitTestCogOutput<typename PublishersType::template UnitTestOutputViewPolicyType<{signals_offset}>>",
                    cpp_namespace="clockwork::testing",
                ),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=signals_body,
                no_discard=True,
            )
            wrapper.public.append(signals_method)
        else:
            struct = CppStruct(
                name=CppType([], f"{self.cog_ir.name}TestWrapperSignals", None),
                doc=f"{self.cog_ir.name}TestWrapperSignals",
            )

            struct.public.append(
                CppTypeAliasDef(
                    name="PublishersType",
                    alias_for=CppType(
                        [],
                        f"typename ::clockwork::testing::UnitTestCog<{self.policy_class_name}>::PublishersType",
                        None,
                    ),
                    doc=None,
                )
            )
            outputs_type = CppType(
                includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
                type_name="UnitTestCogOutputs<PublishersType>",
                cpp_namespace="clockwork::testing",
            )

            struct.private.append(
                CppNamedValue(
                    named_type=CppNamedType(
                        argument_type=outputs_type,
                        argument_name="outputs_",
                    ),
                    value=None,
                    doc=None,
                    qualifiers=None,
                    render_initializer=False,
                )
            )

            struct.public.append(
                CppConstructor(
                    doc=None,
                    arguments=[CppNamedType(outputs_type, "outputs")],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    member_init_list=[("outputs_", "::std::move(outputs)")],
                    body=CppChunk(),
                )
            )

            for index, rg_name in enumerate(self.cog_ir.report_groups.keys()):
                get_body = CppChunk()
                get_body.append(f"return outputs_.{self.template_prefix}get_output<{index + signals_offset}>();")
                get_method = CppMethod(
                    name=f"get_{rg_name}",
                    doc=None,
                    return_type=CppType(
                        includes=[],
                        type_name=f"UnitTestCogOutput<typename PublishersType::template UnitTestOutputViewPolicyType<{index + signals_offset}>>",
                        cpp_namespace="clockwork::testing",
                    ),
                    arguments=[],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=get_body,
                    no_discard=True,
                )
                struct.public.append(get_method)

            wrapper.public.append(struct)

            signals_body = CppChunk()
            signals_body.append(f"return {self.cog_ir.name}TestWrapperSignals{{unit_test_cog_.get_outputs()}};")
            signals_method = CppMethod(
                name="get_signals",
                doc=None,
                return_type=CppType([], f"{self.cog_ir.name}TestWrapper::{self.cog_ir.name}TestWrapperSignals", None),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=signals_body,
                no_discard=True,
            )
            wrapper.public.append(signals_method)

    def _render_test_wrapper(self) -> CppStruct:  # noqa: PLR0915 (rendering function has many statements)
        """Render the C++ code for the cog unit test wrapper.

        Returns:
            CppStruct containing the cog unit test wrapper
        """
        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapper", self.cpp_namespace), doc=f"{self.cog_ir.name}TestWrapper"
        )
        struct.template_param = self.template_params
        struct.template_args = self.template_args

        dummy_cog_queue_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/dummy_cog_queue.hh")],
            type_name="DummyCogQueue",
            cpp_namespace="clockwork::testing",
        )
        unit_test_cog_type = CppType(
            includes=[
                Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh"),
                self.cog_header,
            ],
            type_name=f"UnitTestCog<{self.policy_class_name}>",
            cpp_namespace="clockwork::testing",
        )
        channel_factory_type = CppType(
            includes=[
                Header(CLK_REPO, "clockwork/pinion/abstract_channel_factory.hh"),
                self.cog_header,
            ],
            type_name="AbstractChannelFactory",
            cpp_namespace="clockwork::pinion",
        )

        struct.public.append(
            CppTypeAliasDef(
                name="WrappedCogPolicy",
                alias_for=CppType(
                    [],
                    f"{self.policy_class_name}",
                    None,
                ),
                doc=None,
            )
        )

        struct.public.append(
            CppConstructor(
                doc=None,
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                member_init_list=[
                    (
                        "unit_test_cog_",
                        "::jewels::memory::MemoryResource{::std::pmr::get_default_resource()}, ::jewels::Uuid<::clockwork::common::CogInstanceId>::random_uuid(), ::jewels::memory::make_non_null_from_ref(queue_)",
                    )
                ],
                body=CppChunk(),
            )
        )

        self._render_memory_resources(struct)
        self._render_configs(struct)
        self._render_states(struct)
        self._render_inputs(struct)
        self._render_aligned_inputs(struct)
        self._render_outputs(struct)
        self._render_signals(struct)
        struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(dummy_cog_queue_type, "queue_"),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )
        struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(unit_test_cog_type, "unit_test_cog_"),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        initialize_body = CppChunk()
        initialize_body.append(
            [
                "if (const auto prime_result = unit_test_cog_.prime(timeval, memres); !prime_result)",
                "{",
                '  throw ::std::runtime_error("Failed to prime the unit test cog");',
                "}",
            ]
        )
        initialize_method = CppMethod(
            name="initialize",
            doc=None,
            return_type=VOID,
            arguments=[
                CppNamedType(SYNC_TIME, "timeval"),
                CppNamedType(MEMORY_RESOURCE, "memres"),
            ],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=initialize_body,
            no_discard=False,
        )
        struct.public.append(initialize_method)

        initialize_body2 = CppChunk()
        initialize_body2.append(
            "initialize(timeval, ::jewels::memory::MemoryResource{std::pmr::get_default_resource()});"
        )
        initialize_method2 = CppMethod(
            name="initialize",
            doc=None,
            return_type=VOID,
            arguments=[CppNamedType(SYNC_TIME, "timeval")],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=initialize_body2,
            no_discard=False,
        )
        struct.public.append(initialize_method2)

        execute_body = CppChunk()
        execute_body.append(
            [
                "auto throttled_until = ::jewels::time::SyncTime::min();",
                "if (::jewels::fails(unit_test_cog_.prepare_for_execution(::jewels::Out{throttled_until}, timeval)))",
                "{",
                "  return false;",
                "}",
                "::clockwork::CogExecuteParams params{",
                "  .start_time = timeval,",
                "};",
                "return unit_test_cog_.execute(params, exec_duration).has_value();",
            ]
        )
        execute_method = CppMethod(
            name="execute",
            doc=None,
            return_type=BOOLEAN,
            arguments=[
                CppNamedType(SYNC_TIME, "timeval"),
                CppNamedType(MICROSECONDS, "exec_duration"),
            ],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=execute_body,
            no_discard=True,
        )
        struct.public.append(execute_method)

        execute_body2 = CppChunk()
        execute_body2.append("return execute(timeval, {});")
        execute_method2 = CppMethod(
            name="execute",
            doc=None,
            return_type=BOOLEAN,
            arguments=[CppNamedType(SYNC_TIME, "timeval")],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=execute_body2,
            no_discard=True,
        )
        struct.public.append(execute_method2)

        get_pinion_shm_root_body = CppChunk()
        get_pinion_shm_root_body.append("return unit_test_cog_.get_pinion_shm_root();")
        get_pinion_shm_root_method = CppMethod(
            name="get_pinion_shm_root",
            doc=None,
            return_type=STRING_VIEW,
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=["const"],
            body=get_pinion_shm_root_body,
            no_discard=True,
        )
        struct.public.append(get_pinion_shm_root_method)

        get_pinion_namespace_body = CppChunk()
        get_pinion_namespace_body.append("return unit_test_cog_.get_pinion_namespace();")
        get_pinion_namespace_method = CppMethod(
            name="get_pinion_namespace",
            doc=None,
            return_type=STRING_VIEW,
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=["const"],
            body=get_pinion_namespace_body,
            no_discard=True,
        )
        struct.public.append(get_pinion_namespace_method)

        get_channel_factory_body = CppChunk()
        get_channel_factory_body.append("return unit_test_cog_.get_channel_factory();")
        get_channel_factory_method = CppMethod(
            name="get_channel_factory",
            doc=None,
            return_type=ref_qualify(channel_factory_type, Ref.L),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=get_channel_factory_body,
            no_discard=True,
        )
        struct.public.append(get_channel_factory_method)

        get_unit_test_cog_body = CppChunk()
        get_unit_test_cog_body.append("return unit_test_cog_;")
        get_unit_test_cog_method = CppMethod(
            name="get_unit_test_cog",
            doc=None,
            return_type=ref_qualify(unit_test_cog_type, Ref.L),
            arguments=[],
            leading_qualifiers=[],
            trailing_qualifiers=[],
            body=get_unit_test_cog_body,
            no_discard=True,
        )
        struct.public.append(get_unit_test_cog_method)

        return struct

    def _render_combo_wrapper(self) -> CppStruct:  # noqa: PLR0915 (rendering function has many statements)
        """Render the combo wrapper class for aligner + consumer end-to-end testing.

        The combo wrapper owns both an aligner test wrapper and a consumer test wrapper,
        wires them together, and provides convenience methods for publishing to shared
        upstream channels and executing both cogs.

        Returns:
            CppStruct containing the combo wrapper class.
        """
        if not self.cog_ir.aligned_inputs:
            msg = "cpp_combo_test requires aligned_inputs"
            raise ValueError(msg)

        # The aligned input runtime codegen already enforces only one aligned input per consumer
        ((_group_name, aligned_def),) = self.cog_ir.aligned_inputs.items()

        aligner_type = aligned_def.aligned_type
        if not isinstance(aligner_type, aligner_ir.Aligner):
            msg = f"Expected Aligner, got {type(aligner_type).__name__}"
            raise TypeError(msg)
        resolved = aligner_type.get_resolved()
        aligner_module = aligner_type.get_module()

        aligner_module_name = aligner_module.module_id.name.split("::")[-1]
        aligner_include_dir = aligner_module.module_id.get_base_path().parent
        aligner_test_header = str(aligner_include_dir / f"{aligner_module_name}_clk_cc_test.hh")

        assert aligner_module.inner_attrs is not None
        aligner_namespace = aligner_module.inner_attrs.get_cpp_namespace()
        assert aligner_namespace is not None

        aligner_cog_name = aligner_type.name
        aligner_wrapper_type_name = f"{aligner_cog_name}TestWrapper"
        aligner_inputs_type_name = f"{aligner_cog_name}TestWrapperInputs"

        consumer_wrapper_type_name = f"{self.cog_ir.name}TestWrapper"
        consumer_inputs_type_name = f"{self.cog_ir.name}TestWrapperInputs"

        combo_struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}ComboWrapper", self.cpp_namespace),
            doc=f"{self.cog_ir.name}ComboWrapper",
        )

        aligner_wrapper_cpp_type = CppType(
            includes=[Header(aligner_module.module_id.repo, aligner_test_header)],
            type_name=aligner_wrapper_type_name,
            cpp_namespace=aligner_namespace,
        )
        consumer_wrapper_cpp_type = CppType(
            includes=[self.cog_header],
            type_name=consumer_wrapper_type_name,
            cpp_namespace=self.cpp_namespace,
        )

        combo_struct.public.append(
            CppTypeAliasDef(
                name="AlignerWrapperType",
                alias_for=CppType(
                    includes=[Header(aligner_module.module_id.repo, aligner_test_header)],
                    type_name=aligner_wrapper_type_name,
                    cpp_namespace=aligner_namespace,
                ),
                doc=None,
            )
        )
        combo_struct.public.append(
            CppTypeAliasDef(
                name="ConsumerWrapperType",
                alias_for=CppType([], consumer_wrapper_type_name, self.cpp_namespace),
                doc=None,
            )
        )
        fq_aligner_inputs_type = f"{aligner_namespace}::{aligner_wrapper_type_name}::{aligner_inputs_type_name}"
        combo_struct.public.append(
            CppTypeAliasDef(
                name="AlignerInputsType",
                alias_for=CppType([], fq_aligner_inputs_type, None),
                doc=None,
            )
        )

        # No explicit constructor — compiler-generated default is fine.
        # The aligner_inputs_ and consumer_inputs_ handles are set during initialize().

        combo_struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(aligner_wrapper_cpp_type, "aligner_"),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )
        combo_struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(consumer_wrapper_cpp_type, "consumer_"),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=False,
            )
        )

        optional_aligner_inputs_type = CppType(
            includes=[SystemHeader("optional")],
            type_name="std::optional<AlignerInputsType>",
            cpp_namespace=None,
        )
        combo_struct.private.append(
            CppNamedValue(
                named_type=CppNamedType(optional_aligner_inputs_type, "aligner_inputs_"),
                value=None,
                doc=None,
                qualifiers=None,
                render_initializer=True,
            )
        )

        # Consumer inputs type — only needed when the consumer has regular dial inputs
        regular_count = sum(1 for idef in self.cog_ir.inputs.values() if not idef.view_params.no_dial)
        has_regular_inputs = regular_count > 0

        if has_regular_inputs:
            fq_consumer_wrapper = (
                f"{self.cpp_namespace}::{consumer_wrapper_type_name}"
                if self.cpp_namespace
                else consumer_wrapper_type_name
            )
            fq_consumer_inputs_type = f"{fq_consumer_wrapper}::{consumer_inputs_type_name}"

            optional_consumer_inputs_type = CppType(
                includes=[SystemHeader("optional")],
                type_name=f"std::optional<{fq_consumer_inputs_type}>",
                cpp_namespace=None,
            )
            combo_struct.private.append(
                CppNamedValue(
                    named_type=CppNamedType(optional_consumer_inputs_type, "consumer_inputs_"),
                    value=None,
                    doc=None,
                    qualifiers=None,
                    render_initializer=True,
                )
            )

        # --- initialize() methods ---
        alignment_msg_index = regular_count
        upstream_start_index = regular_count + 1

        init_body = CppChunk()
        init_body.append(
            [
                "aligner_.initialize(timeval, memres);",
                "consumer_.initialize(timeval, memres);",
                "",
                "aligner_inputs_ = aligner_.get_inputs();",
                *(
                    [
                        "consumer_inputs_ = consumer_.get_inputs();",
                    ]
                    if has_regular_inputs
                    else []
                ),
                "",
                f"consumer_.get_unit_test_cog().template rewire_input<{alignment_msg_index}>(",
                "  aligner_.get_unit_test_cog().template get_output_publisher<0>());",
            ]
        )

        for aligner_input_index, _input_name in enumerate(resolved.inputs):
            consumer_input_index = upstream_start_index + aligner_input_index
            init_body.append(
                f"consumer_.get_unit_test_cog().template rewire_input<{consumer_input_index}>("
                + f"\n  aligner_.get_unit_test_cog().template get_input_publisher<{aligner_input_index}>());"
            )

        combo_struct.public.append(
            CppMethod(
                name="initialize",
                doc=None,
                return_type=VOID,
                arguments=[
                    CppNamedType(SYNC_TIME, "timeval"),
                    CppNamedType(MEMORY_RESOURCE, "memres"),
                ],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=init_body,
                no_discard=False,
            )
        )

        init_body2 = CppChunk()
        init_body2.append("initialize(timeval, ::jewels::memory::MemoryResource{std::pmr::get_default_resource()});")
        combo_struct.public.append(
            CppMethod(
                name="initialize",
                doc=None,
                return_type=VOID,
                arguments=[CppNamedType(SYNC_TIME, "timeval")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=init_body2,
                no_discard=False,
            )
        )

        # --- Upstream publish methods (delegate to aligner inputs) ---
        message_handle_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/wrappers/unit_test_cog.hh")],
            type_name="MessageHandle",
            cpp_namespace="clockwork::testing",
        )

        for input_name in resolved.inputs:
            publish_body = CppChunk()
            publish_body.append(f"return aligner_inputs_->get_{input_name}().publish(init_fn, publish_time);")

            aligner_input_index = list(resolved.inputs.keys()).index(input_name)
            fq_aligner_policy = f"typename {aligner_namespace}::{aligner_cog_name}Policy"
            msg_type = f"typename ::clockwork::testing::UnitTestCog<{fq_aligner_policy}>::InputsType::template PolicyType<{aligner_input_index}>::MsgType"

            combo_struct.public.append(
                CppMethod(
                    name=f"publish_{input_name}",
                    doc=None,
                    return_type=message_handle_type,
                    arguments=[
                        CppNamedType(
                            CppType(
                                includes=[SystemHeader("functional")],
                                type_name=f"std::function<void({msg_type}&)>",
                                cpp_namespace=None,
                                const=True,
                                ref=Ref.L,
                            ),
                            "init_fn",
                        ),
                        CppNamedType(SYNC_TIME, "publish_time"),
                    ],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=publish_body,
                    no_discard=False,
                )
            )

        # --- Consumer regular input publish methods ---
        dial_inputs = [input_def for input_def in self.cog_ir.inputs.values() if not input_def.view_params.no_dial]
        for input_def in dial_inputs:
            publish_body = CppChunk()
            publish_body.append(f"return consumer_inputs_->get_{input_def.name}().publish(init_fn, publish_time);")

            consumer_input_index = [
                idef.name for idef in self.cog_ir.inputs.values() if not idef.view_params.no_dial
            ].index(input_def.name)
            consumer_policy = f"{self.cog_ir.name}Policy"
            fq_consumer_policy = f"{self.cpp_namespace}::{consumer_policy}" if self.cpp_namespace else consumer_policy
            msg_type = f"typename ::clockwork::testing::UnitTestCog<{fq_consumer_policy}>::InputsType::template PolicyType<{consumer_input_index}>::MsgType"

            combo_struct.public.append(
                CppMethod(
                    name=f"publish_{input_def.name}",
                    doc=None,
                    return_type=message_handle_type,
                    arguments=[
                        CppNamedType(
                            CppType(
                                includes=[SystemHeader("functional")],
                                type_name=f"std::function<void({msg_type}&)>",
                                cpp_namespace=None,
                                const=True,
                                ref=Ref.L,
                            ),
                            "init_fn",
                        ),
                        CppNamedType(SYNC_TIME, "publish_time"),
                    ],
                    leading_qualifiers=[],
                    trailing_qualifiers=[],
                    body=publish_body,
                    no_discard=False,
                )
            )

        # --- Execute methods ---
        exec_aligner_body = CppChunk()
        exec_aligner_body.append("return aligner_.execute(timeval);")
        combo_struct.public.append(
            CppMethod(
                name="execute_aligner",
                doc=None,
                return_type=BOOLEAN,
                arguments=[CppNamedType(SYNC_TIME, "timeval")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=exec_aligner_body,
                no_discard=True,
            )
        )

        exec_consumer_body = CppChunk()
        exec_consumer_body.append("return consumer_.execute(timeval);")
        combo_struct.public.append(
            CppMethod(
                name="execute_consumer",
                doc=None,
                return_type=BOOLEAN,
                arguments=[CppNamedType(SYNC_TIME, "timeval")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=exec_consumer_body,
                no_discard=True,
            )
        )

        exec_both_body = CppChunk()
        exec_both_body.append(
            [
                "if (!execute_aligner(timeval))",
                "{",
                "  return false;",
                "}",
                "return execute_consumer(timeval);",
            ]
        )
        combo_struct.public.append(
            CppMethod(
                name="execute",
                doc=None,
                return_type=BOOLEAN,
                arguments=[CppNamedType(SYNC_TIME, "timeval")],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=exec_both_body,
                no_discard=True,
            )
        )

        # --- Wrapper access ---
        get_aligner_body = CppChunk()
        get_aligner_body.append("return aligner_;")
        combo_struct.public.append(
            CppMethod(
                name="get_aligner",
                doc=None,
                return_type=ref_qualify(aligner_wrapper_cpp_type, Ref.L),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=get_aligner_body,
                no_discard=True,
            )
        )

        get_consumer_body = CppChunk()
        get_consumer_body.append("return consumer_;")
        combo_struct.public.append(
            CppMethod(
                name="get_consumer",
                doc=None,
                return_type=ref_qualify(consumer_wrapper_cpp_type, Ref.L),
                arguments=[],
                leading_qualifiers=[],
                trailing_qualifiers=[],
                body=get_consumer_body,
                no_discard=True,
            )
        )

        return combo_struct

    def render(self) -> CppModuleChunks:
        """Render the C++ implementation for the test cog wrapper.

        Returns:
            CppModuleChunks for the generated C++ implementation.
        """
        self.template_params = get_template_params(self.cog_ir.module.context, self.cog_ir.parameters)
        self.template_args = get_template_args(self.cog_ir.parameters)

        if self.template_params:
            self.policy_class_name = CppTemplateType(
                [],
                f"{self.cog_ir.name}Policy",
                None,
                self.template_args,
            ).render("")
            self.template_prefix = "template "
            self.typename_prefix = "typename "
        else:
            self.policy_class_name = CppType(
                [],
                f"{self.cog_ir.name}Policy",
                None,
            ).render("")

        cpp_mod = CppModuleChunks()

        cpp_mod.implementation_chunk.context.add_include(Header(CLK_REPO, "jewels/callsig/outcome.hh"))
        cpp_mod.implementation_chunk.context.add_include(Header(CLK_REPO, "jewels/callsig/outparam.hh"))
        cpp_mod.implementation_chunk.context.add_include(SystemHeader("stdexcept"))

        enclosing_namespace = self.cpp_namespace or ""
        test_wrapper = self._render_test_wrapper()
        cpp_mod.append(test_wrapper.render(enclosing_namespace))

        if self.generate_combo_test and self.cog_ir.aligned_inputs:
            combo_wrapper = self._render_combo_wrapper()
            cpp_mod.append(combo_wrapper.render(enclosing_namespace))

        return cpp_mod


@dataclass
class CppInstantiatedTestCog:
    """Representation of the C++ implementation of an instantiated C++ test cog."""

    instantiation: cog.InstantiatedCog
    cpp_namespace: str | None
    generate_combo_test: bool = False

    def render(self) -> CppModuleChunks:
        """Render the instantiations for the C++ test cog wrapper.

        Returns:
            CppModuleChunks for the generated C++ implementation.
        """
        assert not self.generate_combo_test
        namespace_resolved = self.cpp_namespace or ""
        cpp_mod = CppModuleChunks()
        rendered_template_args = get_rendered_cog_instantiation_args(self.instantiation, namespace_resolved)
        cpp_mod.implementation_chunk.append(
            f"template struct {self.instantiation.cog_ir.name}TestWrapper<{rendered_template_args}>;"
        )
        return cpp_mod
