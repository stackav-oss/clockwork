# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Unit tests for pub_sub."""

from __future__ import annotations

import re
from pathlib import Path

import pytest
from clockwork.dsl.ir import compiler, primitive, pubsub
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
    use clockwork::dsl::tests::support::hellomsg

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
    use clockwork::dsl::tests::support::hellomsg

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
    assert channel.num_slots.value == max_num_messages + 1
