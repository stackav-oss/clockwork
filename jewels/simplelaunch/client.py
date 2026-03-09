# Copyright 2025 Stack AV Co.
# SPDX-License-Identifier: Apache-2.0

"""Client for simplelaunch."""

from typing import Final, TypeAlias

import requests
from jewels.simplelaunch.v1.service_pb2 import (
    GetProcessListResponse,
    ProcessState,
    SimpleLaunchCommand,
)

_DEFAULT_HEADERS: Final = {
    "Accept": "application/x-protobuf",
    "Content-Type": "application/x-protobuf",
}
DEFAULT_PORT: Final = 8080

SimpleLaunchConnectionError: TypeAlias = requests.exceptions.ConnectionError
SimpleLaunchHTTPError: TypeAlias = requests.exceptions.HTTPError


class SimpleLaunchClient:
    """Client for simplelaunch."""

    def __init__(self, host: str, port: int = DEFAULT_PORT, headers: dict[str, str] = _DEFAULT_HEADERS) -> None:
        """Constructor."""
        self.host = host
        self.port = port
        self.headers = headers

    def _send_command(
        self, command: SimpleLaunchCommand | None, path: str | None = None, timeout: int = 5
    ) -> requests.Response:
        """Send simplelaunch a command."""
        path_str = path or ""
        response = requests.post(
            f"http://{self.host}:{self.port}/{path_str}",
            timeout=timeout,
            data=command.SerializeToString() if command else None,
            headers=self.headers,
        )
        response.raise_for_status()
        return response

    def list_procs(self) -> GetProcessListResponse:
        """List processes."""
        response = requests.get(f"http://{self.host}:{self.port}/", timeout=5, headers=self.headers)
        process_list = GetProcessListResponse()
        process_list.ParseFromString(response.content)
        return process_list

    def is_server_running(self) -> bool:
        """Check if a server is running."""
        try:
            self.is_running()
        except SimpleLaunchConnectionError:
            return False
        else:
            return True

    def quit(self) -> None:
        """Stop all processes and exit."""
        # Use a larger timeout here because the server only sends the
        # response once all child processes have exited.  If a child
        # doesn't go down, the server sleeps between escalations of
        # the signal.  So it can take a while sometimes.
        self._send_command(command=None, path="quit", timeout=30)

    def stop(self, name: str) -> None:
        """Stop a process."""
        command = SimpleLaunchCommand()
        command.stop_process = name
        self._send_command(command=command)

    def stop_all(self) -> None:
        """Stop all processes."""
        command = SimpleLaunchCommand()
        command.stop_all_processes = True
        self._send_command(command=command)

    def start(self, name: str) -> None:
        """Start a process."""
        command = SimpleLaunchCommand()
        command.start_process = name
        self._send_command(command=command)

    def resume(self, name: str) -> None:
        """Resume a process."""
        command = SimpleLaunchCommand()
        command.start_process = name
        command.process_args.extend(["--pinion-resume", "dirty_resume"])
        self._send_command(command=command)

    def logs(self, name: str) -> str:
        """Get the log output from a process."""
        command = SimpleLaunchCommand()
        command.get_logs = name
        return self._send_command(command=command).text

    def is_stopped(self) -> bool:
        """Returns true if all processes are stopped."""
        stopped_states = (
            process.state != ProcessState.PROCESS_STATE_RUNNING for process in self.list_procs().process_info
        )

        return all(stopped_states)

    def is_running(self) -> bool:
        """Returns true if all processes are running."""
        running_states = [
            process.state == ProcessState.PROCESS_STATE_RUNNING for process in self.list_procs().process_info
        ]
        return all(running_states)

    def running_pids(self) -> list[int]:
        """Returns list of pids running."""
        return [
            process.pid
            for process in self.list_procs().process_info
            if process.state == ProcessState.PROCESS_STATE_RUNNING
        ]
