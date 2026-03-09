# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Facilities for generating C++ cog test wrappers."""

from dataclasses import dataclass

from clockwork.dsl.cpp.context import CppChunk, CppModuleChunks, Header, SystemHeader
from clockwork.dsl.cpp.types import (
    BOOLEAN,
    MEMORY_RESOURCE,
    MICROSECONDS,
    SYNC_TIME,
    VOID,
    CppConstructor,
    CppMethod,
    CppNamedType,
    CppNamedValue,
    CppStruct,
    CppType,
    CppTypeAliasDef,
)
from clockwork.dsl.ir import (
    cog,
)
from clockwork.dsl.ir.module_id import CLK_REPO


@dataclass
class CppTestCog:
    """Representation of the C++ implementation of a python cog."""

    cog_ir: cog.Cog
    cpp_namespace: str | None
    cog_header: Header

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
                    f"typename ::clockwork::testing::UnitTestCog<{self.cog_ir.name}Policy>::MemoryResourcesType",
                    None,
                ),
                doc=None,
            )
        )

        memory_resources_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/tests/support/unit_test_cog.hh")],
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
            get_body.append(f"return memory_resources_.get_memory_resource<{index}>();")
            get_method = CppMethod(
                name=f"get_{config.name}",
                doc=None,
                return_type=CppType(
                    [],
                    f"const typename ::std::tuple_element_t<{index}, MemoryResourcesType::PoliciesTuple>::MemoryResourceType&",
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
            set_body.append(f"memory_resources_.set_memory_resource<{index}>(::std::move(memory_resource));")
            set_method = CppMethod(
                name=f"set_{config.name}",
                doc=None,
                return_type=VOID,
                arguments=[
                    CppNamedType(
                        CppType(
                            [],
                            f"typename ::std::tuple_element_t<{index}, MemoryResourcesType::PoliciesTuple>::MemoryResourceType",
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
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.cog_ir.name}Policy>::ConfigsType", None
                ),
                doc=None,
            )
        )

        configs_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/tests/support/unit_test_cog.hh")],
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
            get_body.append(f"return configs_.get_config<{index}>();")
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
            get_handle_body.append(f"return configs_.get_config_handle<{index}>();")
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
            set_handle_body.append(f"configs_.set_config_handle<{index}>(::std::move(config_handle));")
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
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.cog_ir.name}Policy>::StatesType", None
                ),
                doc=None,
            )
        )

        states_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/tests/support/unit_test_cog.hh")],
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
            get_body.append(f"return states_.get_state<{index}>();")
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
            get_handle_body.append(f"return states_.get_state_handle<{index}>();")
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
            set_handle_body.append(f"states_.set_state_handle<{index}>(::std::move(state_handle));")
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
        if not self.cog_ir.inputs:
            return

        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapperInputs", None), doc=f"{self.cog_ir.name}TestWrapperInputs"
        )

        struct.public.append(
            CppTypeAliasDef(
                name="InputsType",
                alias_for=CppType(
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.cog_ir.name}Policy>::InputsType", None
                ),
                doc=None,
            )
        )

        inputs_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/tests/support/unit_test_cog.hh")],
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

        for index, cog_input in enumerate(self.cog_ir.inputs.values()):
            get_body = CppChunk()
            get_body.append(f"return inputs_.get_input<{index}>();")
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
                    [], f"typename ::clockwork::testing::UnitTestCog<{self.cog_ir.name}Policy>::PublishersType", None
                ),
                doc=None,
            )
        )

        outputs_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/cog/tests/support/unit_test_cog.hh")],
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
            get_body.append(f"return outputs_.get_output<{index}>();")
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

    def _render_test_wrapper(self) -> CppStruct:
        """Render the C++ code for the cog unit test wrapper.

        Returns:
            CppStruct containing the cog unit test wrapper
        """
        struct = CppStruct(
            name=CppType([], f"{self.cog_ir.name}TestWrapper", self.cpp_namespace), doc=f"{self.cog_ir.name}TestWrapper"
        )

        dummy_cog_queue_type = CppType(
            includes=[Header(CLK_REPO, "clockwork/common/tests/support/dummy_cog_queue.hh")],
            type_name="DummyCogQueue",
            cpp_namespace="clockwork::testing",
        )
        unit_test_cog_type = CppType(
            includes=[
                Header(CLK_REPO, "clockwork/cog/tests/support/unit_test_cog.hh"),
                self.cog_header,
            ],
            type_name=f"UnitTestCog<{self.cog_ir.name}Policy>",
            cpp_namespace="clockwork::testing",
        )

        struct.public.append(
            CppTypeAliasDef(
                name="WrappedCogPolicy",
                alias_for=CppType(
                    [],
                    f"{self.cog_ir.name}Policy",
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
        self._render_outputs(struct)
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
                "if (! unit_test_cog_.prepare_for_execution(timeval).has_value())",
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

        return struct

    def render(self) -> CppModuleChunks:
        """Render the C++ python cog implementation that calls the python implementation.

        Returns:
            CppModuleChunks for the generated C++ implementation.
        """
        cpp_mod = CppModuleChunks()

        cpp_mod.implementation_chunk.context.add_include(SystemHeader("stdexcept"))

        test_wrapper = self._render_test_wrapper()
        enclosing_namespace = self.cpp_namespace or ""
        cpp_mod.append(test_wrapper.render(enclosing_namespace))

        return cpp_mod
