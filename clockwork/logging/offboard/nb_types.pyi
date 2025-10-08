import clockwork.logging.readers.nb_types

class LoggedChannelMetadata:
    def __init__(
        self,
        channel_name: str,
        message_encoding: str,
        channel_type: str,
        schema_name: str,
        schema_encoding: str,
        schema_definition: bytes,
    ) -> None:
        """Constructor."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def channel_name(self) -> str:
        """Channel name."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def message_encoding(self) -> str:
        """Message encoding."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def channel_type(self) -> str:
        """Channel type."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def schema_name(self) -> str:
        """Schema name."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def schema_encoding(self) -> str:
        """Schema encoding."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def schema_definition(self) -> bytes:
        """Schema definition."""  # noqa: PYI021 (Nanobind stubs need docstrings)

class LoggedMessage:
    def __init__(
        self,
        channel_name: str,
        sequence_number: int,
        transmit_time: clockwork.logging.readers.nb_types.LogTimestamp,
        log_time: clockwork.logging.readers.nb_types.LogTimestamp,
        header: memoryview,
        data: memoryview,
        is_repeated_persistent: bool,
        message_encoding: str,
        is_lite_compressed: bool,
    ) -> None: ...
    @property
    def channel_name(self) -> str: ...
    @property
    def sequence_number(self) -> int: ...
    @property
    def log_time(self) -> clockwork.logging.readers.nb_types.LogTimestamp: ...
    @property
    def transmit_time(self) -> clockwork.logging.readers.nb_types.LogTimestamp: ...
    @property
    def header(self) -> bytes:
        """Message header copy."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def header_view(self) -> memoryview[int]:
        """Message header view."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def data(self) -> bytes:
        """Message data copy."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def data_view(self) -> memoryview[int]:
        """Message data view."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def is_repeated_persistent(self) -> bool:
        """Is the message a persistent message repeated at the start of the log."""  # noqa: PYI021 (Nanobind stubs need docstrings)

    @property
    def message_encoding(self) -> str: ...
    @property
    def is_lite_compressed(self) -> bool:
        """Is the message lite compressed."""  # noqa: PYI021 (Nanobind stubs need docstrings)
