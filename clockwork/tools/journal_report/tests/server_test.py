# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0
# pyright: reportPrivateUsage=false

"""Tests for local journal report serving and lazy Tachyon decoding."""

from __future__ import annotations

import json
from contextlib import contextmanager
from dataclasses import dataclass
from enum import Enum
from http.client import HTTPConnection
from threading import Thread
from typing import TYPE_CHECKING, final
from uuid import UUID

import pytest
from clockwork.journal import journal_pb2
from clockwork.tools.journal_report.request import (
    DEFAULT_SERVER_HOST,
    RequestValidationError,
    create_serve_request,
)
from clockwork.tools.journal_report.server import IndexedMessageLookup, make_report_server

if TYPE_CHECKING:
    from collections.abc import Collection, Generator, Iterator, Mapping
    from http.server import HTTPServer
    from pathlib import Path


@final
class _Mode(Enum):
    ENABLED = 1


@final
@dataclass(frozen=True, kw_only=True)
class _TachyonMessage:
    name: str
    identifier: UUID
    mode: _Mode


@final
@dataclass(frozen=True, kw_only=True)
class _LoggedMessage:
    topic: str
    sequence_number: int
    message: object


@final
class _Reader:
    def __init__(self, messages: tuple[_LoggedMessage, ...]) -> None:
        self.logged_messages = messages
        self.added_topics: list[str] = []
        self.requests: list[Mapping[str, int | Collection[int]] | None] = []

    def add_topic(self, topic: str) -> None:
        self.added_topics.append(topic)

    def messages(self, sequence_numbers: Mapping[str, int | Collection[int]] | None = None) -> Iterator[_LoggedMessage]:
        self.requests.append(sequence_numbers)
        yield from self.logged_messages


@final
class _ReaderFactory:
    def __init__(self, messages: tuple[_LoggedMessage, ...]) -> None:
        self.messages = messages
        self.readers: list[_Reader] = []

    def __call__(self) -> _Reader:
        reader = _Reader(self.messages)
        self.readers.append(reader)
        return reader


@final
class _MessageLookup:
    def __init__(self, messages: Mapping[tuple[str, int], bytes]) -> None:
        self.messages = messages
        self.requests: list[tuple[str, int]] = []

    def lookup(self, channel_name: str, sequence_number: int) -> bytes | None:
        self.requests.append((channel_name, sequence_number))
        return self.messages.get((channel_name, sequence_number))


def _journal() -> journal_pb2.JournalFile:
    return journal_pb2.JournalFile(
        metadata=journal_pb2.JournalMetadata(
            start_time_ns=10,
            end_time_ns=20,
            log_uri="/logs/demo.clog",
            generator_version="test",
        ),
        channel_messages=[
            journal_pb2.ChannelMessage(channel_name="channel/a", sequence_number=7, payload_size_bytes=7)
        ],
    )


@contextmanager
def _running_server(
    message_lookup: _MessageLookup,
    *,
    host: str = DEFAULT_SERVER_HOST,
) -> Generator[HTTPServer]:
    server = make_report_server(
        _journal(),
        host=host,
        port=0,
        message_lookup=message_lookup,
        title="Server Test",
    )
    thread = Thread(target=server.serve_forever)
    thread.start()
    try:
        yield server
    finally:
        server.shutdown()
        thread.join()
        server.server_close()


def _get(server: HTTPServer, path: str) -> tuple[int, str, bytes]:
    connection = HTTPConnection(DEFAULT_SERVER_HOST, server.server_port)
    connection.request("GET", path)
    response = connection.getresponse()
    result = (response.status, response.getheader("Content-Type", ""), response.read())
    connection.close()
    return result


def test_serve_request_defaults_local(tmp_path: Path) -> None:
    journal_path = tmp_path / "demo.journal.pb"
    journal_path.write_bytes(_journal().SerializeToString(deterministic=True))

    local_request = create_serve_request(journal=journal_path)

    assert local_request.host == "127.0.0.1"
    assert local_request.port == 0


def test_serve_request_rejects_invalid_port(tmp_path: Path) -> None:
    journal_path = tmp_path / "demo.journal.pb"
    journal_path.write_bytes(_journal().SerializeToString(deterministic=True))

    with pytest.raises(RequestValidationError, match="between 0 and 65535"):
        create_serve_request(journal=journal_path, port=65536)


def test_message_lookup_lazily_decodes_only_indexed_channel_and_sequence() -> None:
    identifier = UUID("12345678-1234-5678-1234-567812345678")
    reader_factory = _ReaderFactory(
        (
            _LoggedMessage(
                topic="channel/a",
                sequence_number=7,
                message=_TachyonMessage(name="example", identifier=identifier, mode=_Mode.ENABLED),
            ),
        )
    )
    unsupported_sequence = 2**32
    lookup = IndexedMessageLookup(
        reader_factory=reader_factory,
        allowed_messages=frozenset({("channel/a", 7), ("channel/a", unsupported_sequence)}),
    )

    decoded = lookup.lookup("channel/a", 7)
    repeated_decoded = lookup.lookup("channel/a", 7)

    assert decoded is not None
    assert json.loads(decoded) == {
        "identifier": identifier.hex,
        "mode": "ENABLED",
        "name": "example",
    }
    assert repeated_decoded == decoded
    assert lookup.lookup("channel/a", 8) is None
    assert lookup.lookup("channel/a", unsupported_sequence) is None
    assert [reader.added_topics for reader in reader_factory.readers] == [["channel/a"], ["channel/a"]]
    assert [reader.requests for reader in reader_factory.readers] == [[{"channel/a": 7}], [{"channel/a": 7}]]


def test_server_serves_html_report_data_and_lazy_message_json() -> None:
    message_json = b'{"value":42}'
    message_lookup = _MessageLookup({("channel/a", 7): message_json})

    with _running_server(message_lookup) as server:
        html_response = _get(server, "/")
        message_response = _get(server, "/api/message?channel=channel%2Fa&sequence=7")
        raw_payload_response = _get(server, "/api/payload?channel=channel%2Fa&sequence=7")

    assert html_response[0:2] == (200, "text/html; charset=utf-8")
    assert b"Server Test" in html_response[2]
    assert b'data-message-data-api="/api/message"' in html_response[2]
    assert message_response == (200, "application/json", message_json)
    assert raw_payload_response[0] == 404


def test_server_all_interface_bind_is_reachable_through_loopback() -> None:
    message_lookup = _MessageLookup({})

    with _running_server(
        message_lookup,
        host="0.0.0.0",  # noqa: S104 - Explicit SVW port-forwarding bind under test.
    ) as server:
        response = _get(server, "/")

    assert response[0:2] == (200, "text/html; charset=utf-8")


def test_server_rejects_message_outside_report_index() -> None:
    message_lookup = _MessageLookup({})

    with _running_server(message_lookup) as server:
        response = _get(server, "/api/message?channel=channel%2Fa&sequence=8")

    assert response[0:2] == (404, "application/json")
    assert message_lookup.requests == [("channel/a", 8)]
