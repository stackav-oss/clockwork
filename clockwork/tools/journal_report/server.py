# Copyright 2025-2026 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Local HTTP serving for journal reports and lazy Tachyon message display."""

from __future__ import annotations

import json
from dataclasses import asdict, dataclass, is_dataclass
from enum import Enum
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, HTTPServer
from typing import TYPE_CHECKING, Final, Protocol, cast, final
from urllib.parse import parse_qs, urlsplit
from uuid import UUID

from clockwork.logging.readers.py_log_reader import LogReader
from clockwork.tools.journal_report.renderer import render_report
from typing_extensions import override

_MAX_LOG_READER_SEQUENCE_NUMBER: Final = 2**32 - 1

if TYPE_CHECKING:
    from collections.abc import Callable, Collection, Iterator, Mapping

    from clockwork.journal import journal_pb2


class DeserializedMessageLike(Protocol):
    """Deserialized log message fields needed for report display."""

    @property
    def topic(self) -> str:
        """Logged channel name."""
        ...

    @property
    def sequence_number(self) -> int:
        """Logged sequence number."""
        ...

    @property
    def message(self) -> object:
        """Dynamically deserialized Tachyon message."""
        ...


class DeserializingReaderLike(Protocol):
    """Log reader API used for targeted Tachyon deserialization."""

    def add_topic(self, topic: str) -> None:
        """Prepare a logged topic for dynamic Tachyon deserialization."""
        ...

    def messages(
        self, sequence_numbers: Mapping[str, int | Collection[int]] | None = None
    ) -> Iterator[DeserializedMessageLike]:
        """Iterate selected deserialized messages."""
        ...


class MessageLookupLike(Protocol):
    """Lookup interface used by the HTTP request handler."""

    def lookup(self, channel_name: str, sequence_number: int) -> bytes | None:
        """Return one allowlisted Tachyon message as JSON."""
        ...


@final
@dataclass(frozen=True, kw_only=True)
class IndexedMessageLookup:
    """Lazy Tachyon JSON lookup restricted to the journal message index."""

    reader_factory: Callable[[], DeserializingReaderLike]
    """Factory for a fresh deserializing source-log reader per lookup."""

    allowed_messages: frozenset[tuple[str, int]]
    """Channel and sequence pairs present in the journal report index."""

    def lookup(self, channel_name: str, sequence_number: int) -> bytes | None:
        """Deserialize one indexed message and return its JSON representation."""
        if (channel_name, sequence_number) not in self.allowed_messages:
            return None
        if sequence_number > _MAX_LOG_READER_SEQUENCE_NUMBER:
            return None

        reader = self.reader_factory()
        reader.add_topic(channel_name)
        for message in reader.messages(sequence_numbers={channel_name: sequence_number}):
            if message.topic == channel_name and message.sequence_number == sequence_number:
                return _tachyon_json(message.message)
        return None


def make_message_lookup(journal: journal_pb2.JournalFile, *, log_uri: str) -> IndexedMessageLookup:
    """Create an allowlisted lazy Tachyon lookup backed by the source log."""
    allowed_messages = frozenset(
        (message.channel_name, message.sequence_number) for message in journal.channel_messages
    )
    return IndexedMessageLookup(reader_factory=lambda: LogReader(log_uri), allowed_messages=allowed_messages)


@final
class _TachyonJsonEncoder(json.JSONEncoder):
    """JSON encoder for non-primitive values used by Tachyon dataclasses."""

    @override
    def default(self, o: object) -> object:
        if isinstance(o, UUID):
            return o.hex
        if isinstance(o, Enum):
            return o.name
        return cast("object", super().default(o))


def _tachyon_json(message: object) -> bytes:
    """Convert one dynamically deserialized Tachyon dataclass to JSON bytes."""
    if not is_dataclass(message) or isinstance(message, type):
        msg = "Deserialized Tachyon message is not a dataclass."
        raise TypeError(msg)
    return json.dumps(asdict(message), cls=_TachyonJsonEncoder, ensure_ascii=False, sort_keys=True).encode()


def make_report_server(
    journal: journal_pb2.JournalFile,
    *,
    host: str,
    port: int,
    message_lookup: MessageLookupLike,
    title: str | None = None,
) -> HTTPServer:
    """Create a report HTTP server without starting its request loop."""
    report_html = render_report(journal, title=title, message_data_api="/api/message").encode()
    handler = _make_handler(report_html=report_html, message_lookup=message_lookup)
    return HTTPServer((host, port), handler)


def _make_handler(  # noqa: C901 - Complexity is the sum of the small methods on this closure-backed handler.
    *, report_html: bytes, message_lookup: MessageLookupLike
) -> type[BaseHTTPRequestHandler]:
    @final
    class ReportRequestHandler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            request_url = urlsplit(self.path)
            if request_url.path == "/":
                self._send_bytes(HTTPStatus.OK, "text/html; charset=utf-8", report_html)
                return
            if request_url.path == "/api/message":
                self._serve_message(request_url.query)
                return
            self._send_json_error(HTTPStatus.NOT_FOUND, "Route not found.")

        def _serve_message(self, query: str) -> None:
            parameters = parse_qs(query, keep_blank_values=True)
            channels = parameters.get("channel", [])
            sequences = parameters.get("sequence", [])
            if len(channels) != 1 or len(sequences) != 1:
                self._send_json_error(HTTPStatus.BAD_REQUEST, "Specify one channel and sequence.")
                return
            try:
                sequence_number = int(sequences[0])
            except ValueError:
                self._send_json_error(HTTPStatus.BAD_REQUEST, "Sequence must be a non-negative integer.")
                return
            if sequence_number < 0:
                self._send_json_error(HTTPStatus.BAD_REQUEST, "Sequence must be a non-negative integer.")
                return

            try:
                message_json = message_lookup.lookup(channels[0], sequence_number)
            except (OSError, RuntimeError, TypeError, ValueError):
                self._send_json_error(HTTPStatus.INTERNAL_SERVER_ERROR, "Message decoding failed.")
                return
            if message_json is None:
                self._send_json_error(HTTPStatus.NOT_FOUND, "Indexed message not found.")
                return
            self._send_bytes(HTTPStatus.OK, "application/json", message_json)

        def _send_json_error(self, status: HTTPStatus, message: str) -> None:
            body = json.dumps({"error": message}, separators=(",", ":")).encode()
            self._send_bytes(status, "application/json", body)

        def _send_bytes(self, status: HTTPStatus, content_type: str, body: bytes) -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        @override
        def log_message(self, format: str, *args: object) -> None:
            return

    return ReportRequestHandler
