# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

import re
from decimal import Decimal
from pathlib import Path

import pytest
from clockwork.dsl.compiler_context import CompilerContext
from clockwork.dsl.ir import clkbuiltins, compiler, policy, primitive, pubsub, typesys
from clockwork.dsl.ir.importer import FilesystemImporter
from clockwork.dsl.ir.module_id import CLK_REPO, ModuleID


@pytest.fixture()
def fs_importer() -> FilesystemImporter:
    return FilesystemImporter(compile_fn=compiler.compile_source_file)


def test_channel(fs_importer: FilesystemImporter) -> None:
    module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomod.clk")),
        fs_importer,
    )

    registry = module.context[pubsub.CHANNEL_REGISTRY_KEY]
    assert len(registry.channel_registry) == 5
    chan1 = registry.channel_registry["HelloChan"]
    chan2 = registry.channel_registry["Name that doesn't follow reasonable conventions!"]
    chan3 = registry.channel_registry["many_publishers"]
    assert module.inner_scope.lookup("HelloChan") is chan1
    assert module.inner_scope.lookup("AnotherChan") is chan2
    assert module.inner_scope.lookup("MultiPublisherChannel") is chan3
    assert isinstance(chan1.channel_name, primitive.StringValue)
    assert chan1.message_repr is not None
    assert isinstance(chan1.message_size, primitive.DecimalValue)
    assert isinstance(chan1.num_slots, primitive.DecimalValue)
    assert chan1.channel_name.value == "HelloChan"
    assert chan1.message_size.value == 1064
    assert chan1.num_slots.value == 11
    assert chan1.publishers_option == pubsub.ChannelPublishersOption.single
    assert not chan1.is_bulk_data

    assert isinstance(chan2.channel_name, primitive.StringValue)
    assert chan2.message_repr is not None
    assert isinstance(chan2.message_size, primitive.DecimalValue)
    assert isinstance(chan2.num_slots, primitive.DecimalValue)
    assert chan2.channel_name.value == "Name that doesn't follow reasonable conventions!"
    assert chan2.message_size.value == 1064
    assert chan2.num_slots.value == 102
    assert chan2.publishers_option == pubsub.ChannelPublishersOption.single

    assert isinstance(chan3.channel_name, primitive.StringValue)
    assert chan3.message_repr is not None
    assert isinstance(chan3.message_size, primitive.DecimalValue)
    assert isinstance(chan3.num_slots, primitive.DecimalValue)
    assert chan3.channel_name.value == "many_publishers"
    assert chan3.message_size.value == 1064
    assert chan3.num_slots.value == 12
    assert chan3.publishers_option == pubsub.ChannelPublishersOption.multiple

    assert chan1.message_repr is chan2.message_repr


def test_zero_max_num_messages(fs_importer: FilesystemImporter) -> None:
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Doc
    channel ZeroChannelTest
    {
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 0;
    }
    """
    with pytest.raises(
        ValueError,
        match=re.escape("Must specify at least one slot in the channel."),
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_channel_num_slots(fs_importer: FilesystemImporter) -> None:
    max_num_messages = 123
    source = f"""
    use clockwork::dsl::tests::support::hellomsg;

    // Doc
    channel NumSlotTest
    {{
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: {max_num_messages};
    }}
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    channel = module.inner_scope.lookup("NumSlotTest")
    assert isinstance(channel, pubsub.Channel)
    assert not channel.is_published_once
    assert channel.num_slots.value == max_num_messages + 1


def test_is_published_once(fs_importer: FilesystemImporter) -> None:
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Doc
    channel NumSlotTest
    {
      message_type: Tachyon<hellomsg::HelloMsg>;
      published_once: true;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    channel = module.inner_scope.lookup("NumSlotTest")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_published_once
    assert channel.num_slots.value == 1


def test_is_bulk_data(fs_importer: FilesystemImporter) -> None:
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Doc
    channel NumSlotTest
    {
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 2;
      bulk_data: true;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)
    channel = module.inner_scope.lookup("NumSlotTest")
    assert isinstance(channel, pubsub.Channel)
    assert channel.num_slots.value == 3
    assert channel.is_bulk_data


def test_is_published_once_must_be_true(fs_importer: FilesystemImporter) -> None:
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Doc
    channel NumSlotTest
    {
      message_type: Tachyon<hellomsg::HelloMsg>;
      published_once: false;
    }
    """
    with pytest.raises(
        ValueError,
        match="published_once option must be set to true",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_is_published_once_and_multiple_publishers_are_exclusive(fs_importer: FilesystemImporter) -> None:
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Doc
    channel NumSlotTest
    {
      message_type: Tachyon<hellomsg::HelloMsg>;
      published_once: true;
      publishers: multiple;
    }
    """
    with pytest.raises(
        ValueError,
        match="Cannot have multiple publishers when channel is published once",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "foo"), fs_importer)


def test_generic_channel_parameters(fs_importer: FilesystemImporter) -> None:
    """Test that channels can have a parameters block."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // A generic channel with parameters
    channel GenericChan
    {
      parameters
      {
        // The sensor type
        sensor_type: Type;
      }
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 10;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "generic_chan"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()
    assert channel.parameters is not None
    assert len(channel.parameters) == 1
    assert channel.parameters["sensor_type"].cur_name == "sensor_type"
    assert channel.inner_scope is not None


def test_generic_channel_empty_parameters(fs_importer: FilesystemImporter) -> None:
    """Test that channels can have an empty parameters block."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // A channel with empty parameters block
    channel EmptyParamsChan
    {
      parameters
      {
      }
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 10;
    }
    """
    with pytest.raises(
        ValueError,
        match="A channel parameter block cannot be empty",
    ):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "empty_params"), fs_importer)


def test_generic_channel_multiple_parameters(fs_importer: FilesystemImporter) -> None:
    """Test that channels can have multiple parameters."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // A generic channel with multiple parameters
    channel MultiParamChan
    {
      parameters
      {
        // First parameter
        param_a: Type;
        // Second parameter
        param_b: Type;
      }
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 10;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "multi_params"), fs_importer)
    channel = module.inner_scope.lookup("MultiParamChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()
    assert channel.parameters is not None
    assert len(channel.parameters) == 2
    assert channel.parameters["param_a"].cur_name == "param_a"
    assert channel.parameters["param_b"].cur_name == "param_b"


def test_generic_channel_duplicate_parameter_number(fs_importer: FilesystemImporter) -> None:
    """Test that duplicate parameter numbers raise an error."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Bad channel with duplicate parameter numbers
    channel DuplicateParamChan
    {
      parameters
      {
        // First parameter
        param_a: Type;
        // Second parameter with same number
        param_a: Type;
      }
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 10;
    }
    """
    with pytest.raises(ValueError, match="Duplicate parameter name 'param_a'"):
        compiler.compile_source_text(source, ModuleID(CLK_REPO, "duplicate_params"), fs_importer)


def test_non_generic_channel_is_not_generic(fs_importer: FilesystemImporter) -> None:
    """Test that channels without parameters are not generic."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // A non-generic channel
    channel NonGenericChan
    {
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 10;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "non_generic"), fs_importer)
    channel = module.inner_scope.lookup("NonGenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert not channel.is_generic()
    assert channel.parameters is None
    assert not channel.inner_scope.names


def test_generic_channel_not_registered(fs_importer: FilesystemImporter) -> None:
    """Test that generic channels are not registered in the channel registry."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // A generic channel with parameters
    channel GenericChan
    {
      parameters
      {
        // The sensor type
        sensor_type: Type;
      }
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 10;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "generic_not_registered"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()
    # Generic channels are not in the registry
    registry = module.context[pubsub.CHANNEL_REGISTRY_KEY]
    assert "GenericChan" not in registry.channel_registry


def test_instantiated_channel_basic(fs_importer: FilesystemImporter) -> None:
    """Test basic InstantiatedChannel creation from a generic channel."""
    source = """
    use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

    // A generic channel with a message type parameter
    channel GenericChan
    {
      parameters
      {
        // Some number.
        some_number: UInt64;
        // Some string
        some_string: String;
        // Some type
        some_type: Type;
      }
      name: fmt!("/{some_string}/{some_number}");
      message_type: Tachyon<some_type>;
      max_num_messages: some_number;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "instantiated_basic"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()

    hello_msg = module.inner_scope.lookup("HelloMsg")
    assert hello_msg is not None
    assert isinstance(hello_msg, typesys.Value)

    arguments = {
        "some_number": primitive.DecimalValue(type_info=clkbuiltins.UINT64, value=Decimal(123)),
        "some_string": primitive.StringValue.make("hello"),
        "some_type": hello_msg,
    }
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=channel,
        arguments=arguments,
    )

    inst_channel = pubsub.InstantiatedChannel.from_instantiation(instantiation)
    assert isinstance(inst_channel, pubsub.InstantiatedChannel)
    assert inst_channel.channel is channel
    assert inst_channel.arguments == arguments
    assert inst_channel.is_published_once == channel.is_published_once
    assert inst_channel.publishers_option == channel.publishers_option
    assert inst_channel.channel_name.value == "/hello/123"
    assert isinstance(inst_channel.message_type, typesys.Instantiation)
    assert inst_channel.message_type.instantiates is clkbuiltins.TACHYON
    assert inst_channel.num_slots.value == 123 + 1

    assert inst_channel.value_key() == "Channel(/hello/123)"


def test_instantiated_channel_wrong_type() -> None:
    """Test that InstantiatedChannel rejects non-channel instantiations."""
    # Create an instantiation of something other than a channel
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.TYPE_TYPE,
        instantiates=clkbuiltins.INT32,
        arguments={},
    )

    with pytest.raises(TypeError, match="Expected Channel instantiation"):
        pubsub.InstantiatedChannel.from_instantiation(instantiation)


def test_instantiated_channel_non_generic(fs_importer: FilesystemImporter) -> None:
    """Test that InstantiatedChannel rejects non-generic channels."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // A non-generic channel
    channel NonGenericChan
    {
      message_type: Tachyon<hellomsg::HelloMsg>;
      max_num_messages: 10;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "non_generic_reject"), fs_importer)
    channel = module.inner_scope.lookup("NonGenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert not channel.is_generic()

    # Create an instantiation
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=channel,
        arguments={},
    )

    with pytest.raises(ValueError, match="Cannot instantiate a non-generic channel"):
        pubsub.InstantiatedChannel.from_instantiation(instantiation)


def test_policy_on_non_generic_channel(fs_importer: FilesystemImporter) -> None:
    """Test that policies can be applied to non-generic channels."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Schema for TestPolicy
    schema TestPolicySchema
    {
        fields
        {
            // Do a thing?
            #0 do_thing: Bool = true;
        }
    }

    // A test policy for channels
    def policy TestPolicy
    {
        binds_to: Channel;
        schema: TestPolicySchema;
    }

    // A non-generic channel
    channel TestChan
    {
        message_type: Tachyon<hellomsg::HelloMsg>;
        max_num_messages: 10;
    }

    policy TestPolicy for TestChan
    {
        do_thing = false;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "channel_policy_test"), fs_importer)
    channel = module.inner_scope.lookup("TestChan")
    assert isinstance(channel, pubsub.Channel)

    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()

    policy_data = policy.lookup_policy(module, policy_class, channel)
    assert policy_data is not None
    assert policy_data.policy_class is policy_class
    assert policy_data.target is channel
    assert policy_data.data.data["do_thing"] is clkbuiltins.FALSE_VALUE


def test_policy_on_generic_channel(fs_importer: FilesystemImporter) -> None:
    """Test that policies can be applied to generic channels."""
    source = """
    use clockwork::dsl::tests::support::hellomsg;

    // Schema for TestPolicy
    schema TestPolicySchema
    {
        fields
        {
            // Do a thing?
            #0 do_thing: Bool = true;
        }
    }

    // A test policy for channels
    def policy TestPolicy
    {
        binds_to: Channel;
        schema: TestPolicySchema;
    }

    // A generic channel with a parameter
    channel GenericChan
    {
        parameters
        {
            // Channel name suffix
            some_string: String;
        }
        name: fmt!("/{some_string}");
        message_type: Tachyon<hellomsg::HelloMsg>;
        max_num_messages: 10;
    }

    policy TestPolicy for GenericChan
    {
        do_thing = false;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "generic_channel_policy_test"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()

    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()

    # Direct lookup on generic channel should work
    policy_data = policy.lookup_policy(module, policy_class, channel)
    assert policy_data is not None
    assert policy_data.policy_class is policy_class
    assert policy_data.target is channel
    assert policy_data.data.data["do_thing"] is clkbuiltins.FALSE_VALUE


def test_policy_inheritance_from_generic_channel(fs_importer: FilesystemImporter) -> None:
    """Test that instantiated channels inherit policies from their generic channel."""
    source = """
    use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

    // Schema for TestPolicy
    schema TestPolicySchema
    {
        fields
        {
            // Do a thing?
            #0 do_thing: Bool = true;
        }
    }

    // A test policy for channels
    def policy TestPolicy
    {
        binds_to: Channel;
        schema: TestPolicySchema;
    }

    // A generic channel with a parameter
    channel GenericChan
    {
        parameters
        {
            // Channel name suffix
            some_string: String;
        }
        name: fmt!("/{some_string}");
        message_type: Tachyon<HelloMsg>;
        max_num_messages: 10;
    }

    policy TestPolicy for GenericChan
    {
        do_thing = false;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "policy_inheritance_test"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()

    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()

    # Create an instantiated channel
    arguments = {"some_string": primitive.StringValue.make("test")}
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=channel,
        arguments=arguments,
    )
    inst_channel = pubsub.InstantiatedChannel.from_instantiation(instantiation)

    # Looking up policy on instantiated channel should inherit from generic
    policy_data = policy.lookup_policy(module, policy_class, inst_channel)
    assert policy_data is not None
    assert policy_data.policy_class is policy_class
    # The target is the generic channel, not the instantiated one
    assert policy_data.target is channel
    assert policy_data.data.data["do_thing"] is clkbuiltins.FALSE_VALUE


def test_policy_on_instantiated_channel_without_generic_policy(fs_importer: FilesystemImporter) -> None:
    """Test that policies can be applied to instantiated channels when generic has no policy."""
    source = """
    use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

    // Schema for TestPolicy
    schema TestPolicySchema
    {
        fields
        {
            // Do a thing?
            #0 do_thing: Bool = true;
        }
    }

    // A test policy for channels
    def policy TestPolicy
    {
        binds_to: Channel;
        schema: TestPolicySchema;
    }

    // A generic channel with a parameter (no policy applied)
    channel GenericChan
    {
        parameters
        {
            // Channel name suffix
            some_string: String;
        }
        name: fmt!("/{some_string}");
        message_type: Tachyon<HelloMsg>;
        max_num_messages: 10;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "inst_policy_no_generic_test"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()

    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()

    # Create an instantiated channel
    arguments = {"some_string": primitive.StringValue.make("test")}
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=channel,
        arguments=arguments,
    )
    inst_channel = pubsub.InstantiatedChannel.from_instantiation(instantiation)

    # Binding a policy to instantiated channel should succeed since generic has none
    policy_data_with_true = policy_class.evaluate_call(
        ir_node=None, module=module, args=[("do_thing", clkbuiltins.TRUE_VALUE)]
    )
    policy.bind_policy_data(module, policy_data_with_true, inst_channel)

    # Verify the policy was applied
    policy_data = policy.lookup_policy(module, policy_class, inst_channel)
    assert policy_data is not None
    assert policy_data.target is inst_channel
    assert policy_data.data.data["do_thing"] is clkbuiltins.TRUE_VALUE


def test_no_policy_returns_none(fs_importer: FilesystemImporter) -> None:
    """Test that lookup_policy returns None when no policy exists."""
    source = """
    use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

    // Schema for TestPolicy
    schema TestPolicySchema
    {
        fields
        {
            // Do a thing?
            #0 do_thing: Bool = true;
        }
    }

    // A test policy for channels
    def policy TestPolicy
    {
        binds_to: Channel;
        schema: TestPolicySchema;
    }

    // A generic channel with no policy
    channel GenericChan
    {
        parameters
        {
            // Channel name suffix
            some_string: String;
        }
        name: fmt!("/{some_string}");
        message_type: Tachyon<HelloMsg>;
        max_num_messages: 10;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "no_policy_test"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)

    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()

    # Create an instantiated channel
    arguments = {"some_string": primitive.StringValue.make("test")}
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=channel,
        arguments=arguments,
    )
    inst_channel = pubsub.InstantiatedChannel.from_instantiation(instantiation)

    # No policy on generic or instantiated should return None
    assert policy.lookup_policy(module, policy_class, channel) is None
    assert policy.lookup_policy(module, policy_class, inst_channel) is None


def test_multiple_instantiations_inherit_same_policy(fs_importer: FilesystemImporter) -> None:
    """Test that multiple instantiations of a generic channel all inherit the same policy."""
    source = """
    use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

    // Schema for TestPolicy
    schema TestPolicySchema
    {
        fields
        {
            // Do a thing?
            #0 do_thing: Bool = true;
        }
    }

    // A test policy for channels
    def policy TestPolicy
    {
        binds_to: Channel;
        schema: TestPolicySchema;
    }

    // A generic channel with a parameter
    channel GenericChan
    {
        parameters
        {
            // Channel name suffix
            some_string: String;
        }
        name: fmt!("/{some_string}");
        message_type: Tachyon<HelloMsg>;
        max_num_messages: 10;
    }

    policy TestPolicy for GenericChan
    {
        do_thing = false;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "multiple_instantiation_test"), fs_importer)
    channel = module.inner_scope.lookup("GenericChan")
    assert isinstance(channel, pubsub.Channel)

    policy_def = module.inner_scope.lookup("TestPolicy")
    assert isinstance(policy_def, policy.PolicyDef)
    policy_class = policy_def.get_resolved()

    # Create multiple instantiated channels
    inst1 = pubsub.InstantiatedChannel.from_instantiation(
        typesys.Instantiation(
            type_info=clkbuiltins.CHANNEL_TYPE,
            instantiates=channel,
            arguments={"some_string": primitive.StringValue.make("first")},
        )
    )
    inst2 = pubsub.InstantiatedChannel.from_instantiation(
        typesys.Instantiation(
            type_info=clkbuiltins.CHANNEL_TYPE,
            instantiates=channel,
            arguments={"some_string": primitive.StringValue.make("second")},
        )
    )
    inst3 = pubsub.InstantiatedChannel.from_instantiation(
        typesys.Instantiation(
            type_info=clkbuiltins.CHANNEL_TYPE,
            instantiates=channel,
            arguments={"some_string": primitive.StringValue.make("third")},
        )
    )

    # All should inherit the same policy from the generic channel
    policy1 = policy.lookup_policy(module, policy_class, inst1)
    policy2 = policy.lookup_policy(module, policy_class, inst2)
    policy3 = policy.lookup_policy(module, policy_class, inst3)

    assert policy1 is not None
    assert policy2 is not None
    assert policy3 is not None

    # They should all point to the same PolicyData object (from the generic channel)
    assert policy1 is policy2 is policy3
    assert policy1.target is channel
    assert policy1.data.data["do_thing"] is clkbuiltins.FALSE_VALUE


def test_instantiated_channel_with_external_message_type(fs_importer: FilesystemImporter) -> None:
    """Test that from_instantiation resolves external message types via calling_context.

    Verifies the behavior when a generic channel's message type parameter is set
    to a schema defined in a module that is not imported by the channel's own module:

    - Without a calling_context, the representation lookup falls back to the
      channel's module context, which does not contain the external schema's
      Tachyon representation, causing a ValueError.
    - With a calling_context that merges the channel module's context and the
      schema module's context, the representation is found and instantiation succeeds.
    """
    # Compile a channel module that has no schema imports. Its context will not
    # contain any Tachyon representations for schemas from other modules.
    channel_source = """
    // A generic channel for testing external schema resolution.
    channel ExternalTypeChan
    {
        parameters
        {
            // Channel name suffix.
            chan_name: String;
            // Message type passed by the caller.
            msg_type: Type;
        }
        name: fmt!("/{chan_name}");
        message_type: Tachyon<msg_type>;
        max_num_messages: 10;
    }
    """
    channel_module = compiler.compile_source_text(
        channel_source, ModuleID(CLK_REPO, "test_external_type_channel"), fs_importer
    )

    # Compile the schema module directly from the filesystem. Its context will hold
    # the Tachyon<HelloMsg> representation, which the channel module does not know about.
    hellomsg_module = compiler.compile_source_file(
        ModuleID.from_path(CLK_REPO, Path("clockwork/dsl/tests/support/hellomsg.clk")),
        fs_importer,
    )

    channel = channel_module.inner_scope.lookup("ExternalTypeChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()

    hello_msg_type = hellomsg_module.inner_scope.lookup("HelloMsg")
    assert isinstance(hello_msg_type, typesys.Value)

    arguments = {
        "chan_name": primitive.StringValue.make("test"),
        "msg_type": hello_msg_type,
    }
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=channel,
        arguments=arguments,
    )

    # Without calling_context, the channel module's context is used for representation
    # lookup. The channel module never imported hellomsg, so Tachyon<HelloMsg> is not
    # registered there, and instantiation fails.
    with pytest.raises(ValueError, match="Representation not instantiated or instantiation not visible here"):
        pubsub.InstantiatedChannel.from_instantiation(instantiation)

    # Build a calling_context that merges both the channel and schema module contexts,
    # simulating a calling module that imports both.
    calling_context = CompilerContext("test_calling_module")
    calling_context.import_from(channel_module.context)
    calling_context.import_from(hellomsg_module.context)

    # With calling_context, Tachyon<HelloMsg> is resolvable and instantiation succeeds.
    inst_channel = pubsub.InstantiatedChannel.from_instantiation(instantiation, calling_context)
    assert isinstance(inst_channel, pubsub.InstantiatedChannel)
    assert inst_channel.channel_name.value == "/test"
    assert inst_channel.channel is channel
    assert inst_channel.message_size is not None


def test_instantiated_channel_with_constant_parameter(fs_importer: FilesystemImporter) -> None:
    """Test that from_instantiation accepts module-level constants as parameter values.

    Module-level constants (e.g. ``MAX_SLOTS: UInt64 = 10;``) are represented as
    ImmutableBinding nodes in the IR.  When passed as channel instantiation arguments
    they must be unwrapped to their underlying values so that numeric checks succeed.
    """
    source = """
    use clockwork::dsl::tests::support::hellomsg::{HelloMsg};

    // Maximum number of messages for the test channel.
    MAX_SLOTS: UInt64 = 42;

    // A generic channel whose slot count is supplied by the caller.
    channel SlotCountChan
    {
        parameters
        {
            // Number of message slots.
            num_slots: UInt64;
        }
        message_type: Tachyon<HelloMsg>;
        max_num_messages: num_slots;
    }
    """
    module = compiler.compile_source_text(source, ModuleID(CLK_REPO, "test_constant_param"), fs_importer)

    channel = module.inner_scope.lookup("SlotCountChan")
    assert isinstance(channel, pubsub.Channel)
    assert channel.is_generic()

    # Retrieve the constant as an ImmutableBinding from the module scope.
    max_slots_binding = module.inner_scope.lookup("MAX_SLOTS")
    assert max_slots_binding is not None
    assert isinstance(max_slots_binding, typesys.Value)

    arguments = {"num_slots": max_slots_binding}
    instantiation = typesys.Instantiation(
        type_info=clkbuiltins.CHANNEL_TYPE,
        instantiates=channel,
        arguments=arguments,
    )

    # Passing a module-level constant (ImmutableBinding) as num_slots must succeed;
    # the binding is unwrapped to the underlying DecimalValue before slot resolution.
    inst_channel = pubsub.InstantiatedChannel.from_instantiation(instantiation)
    assert isinstance(inst_channel, pubsub.InstantiatedChannel)
    # num_slots is 42 + 1 buffer slot = 43
    assert inst_channel.num_slots.value == 43
