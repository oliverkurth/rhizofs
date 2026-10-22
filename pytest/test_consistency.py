"""Filesystem consistency through real mounts, including the default caches."""

import os
from pathlib import Path
import tempfile
import time

import pytest

from common import (start_server_fg, stop_server_fg,
                    start_client_fg, stop_client_fg)


def wait_for(predicate, process, description):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        assert process.poll() is None, f"{description}: process exited"
        if predicate():
            return
        time.sleep(0.05)
    pytest.fail(f"timed out waiting for {description}")


@pytest.fixture(params=[None, 0], ids=["default-cache", "no-cache"])
def mounted_share(request):
    # Keep the IPC socket below the Unix socket path limit even when pytest's
    # temporary directory has a long name. Never use the repository's pidfile.
    with tempfile.TemporaryDirectory(prefix="rhizo-consistency-") as work:
        root = Path(work)
        served = root / "served"
        served.mkdir()
        socket = root / "server.sock"
        endpoint = f"ipc://{socket}"
        server = start_server_fg(endpoint, str(served))
        clients = []
        try:
            wait_for(socket.exists, server, "server socket")

            def mount(name):
                directory = root / name
                directory.mkdir()
                args = ([] if request.param is None else
                        [f"--attr-cache-timeout={request.param}"])
                client = start_client_fg(endpoint, str(directory), args=args)
                clients.append(client)
                wait_for(lambda: os.path.ismount(directory), client, "mount")
                return directory

            yield served, mount
        finally:
            try:
                for client in reversed(clients):
                    stop_client_fg(client)
            finally:
                stop_server_fg(server)


@pytest.mark.parametrize("old_size,new_size", [(100, 300), (300, 100)])
def test_rename_over_cached_file(mounted_share, old_size, new_size):
    served, mount = mounted_share
    directory = mount("client")
    target = directory / "target"
    replacement = directory / "replacement"
    target.write_bytes(b"A" * old_size)
    assert target.read_bytes() == b"A" * old_size
    assert target.stat().st_size == old_size
    replacement.write_bytes(b"B" * new_size)

    replacement.replace(target)

    assert target.stat().st_size == new_size
    assert target.read_bytes() == b"B" * new_size
    assert (served / "target").read_bytes() == b"B" * new_size
