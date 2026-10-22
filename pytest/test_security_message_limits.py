"""Check payload and transport limits independently over the wire."""

from pathlib import Path
import tempfile
import time

import pytest

from common import start_server_fg, stop_server_fg, run_rawclient


@pytest.mark.parametrize("size,expected", [
    (64 * 1024 * 1024, "accepted"),
    (64 * 1024 * 1024 + 1, "invalid"),
    (65 * 1024 * 1024 + 1, "disconnected"),
])
def test_write_payload_and_frame_limits(size, expected):
    with tempfile.TemporaryDirectory(prefix="rhizo-frame-") as work:
        root = Path(work)
        served = root / "served"
        served.mkdir()
        socket = root / "server.sock"
        endpoint = f"ipc://{socket}"
        server = start_server_fg(endpoint, str(served))
        try:
            deadline = time.monotonic() + 10
            while not socket.exists():
                assert server.poll() is None
                assert time.monotonic() < deadline
                time.sleep(0.01)
            result = run_rawclient(endpoint, "writeplain", "/target", str(size))
            target = served / "target"
            if expected == "accepted":
                assert result["ERRNO"] == "0"
                assert result["SIZE"] == str(size)
                assert target.stat().st_size == size
                with target.open("rb") as f:
                    assert f.read(1) == b"x"
                    f.seek(-1, 2)
                    assert f.read(1) == b"x"
            elif expected == "invalid":
                assert result["ERRNO"] == "15"  # invalid request, not a timeout
                assert result["HASSIZE"] == "0"
                assert not target.exists()
            else:
                assert result["_retval"] != 0
                assert result["TIMEOUT"] == "1"
                assert not target.exists()
            assert run_rawclient(endpoint, "ping")["ERRNO"] == "0"
        finally:
            stop_server_fg(server)
