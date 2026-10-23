"""Filesystem consistency through real mounts, including the default caches."""

from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import tempfile
import threading
import time

import pytest

from common import (start_server_fg, stop_server_fg,
                    start_client_fg, stop_client_fg, run_rawclient)


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

            def mount(name, extra_args=()):
                directory = root / name
                directory.mkdir()
                args = ([] if request.param is None else
                        [f"--attr-cache-timeout={request.param}"])
                client = start_client_fg(endpoint, str(directory),
                                         args=args + list(extra_args))
                clients.append(client)
                wait_for(lambda: os.path.ismount(directory), client, "mount")
                return directory

            yield served, mount, endpoint
        finally:
            try:
                for client in reversed(clients):
                    stop_client_fg(client)
            finally:
                stop_server_fg(server)


@pytest.mark.parametrize("old_size,new_size", [(100, 300), (300, 100)])
def test_rename_over_cached_file(mounted_share, old_size, new_size):
    served, mount, _ = mounted_share
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


@pytest.mark.parametrize("truncate", [False, True], ids=["keep", "truncate"])
def test_create_over_readdir_cached_file(mounted_share, truncate):
    served, mount, _ = mounted_share
    directory = mount("client", extra_args=["-onegative_timeout=60"])
    target = directory / "target"
    # Keep a negative kernel entry while readdir prefetches the attributes
    # of a file created on the server. The next open then reaches CREATE
    # even though the file already exists remotely.
    assert not target.exists()
    remote = served / "target"
    remote.write_bytes(b"A" * 100)
    assert target in list(directory.iterdir())
    remote.write_bytes(b"B" * 300)

    flags = os.O_WRONLY | os.O_CREAT | (os.O_TRUNC if truncate else 0)
    fd = os.open(target, flags, 0o600)
    os.close(fd)

    expected = b"" if truncate else b"B" * 300
    assert remote.read_bytes() == expected
    assert target.stat().st_size == len(expected)
    assert target.read_bytes() == expected


@pytest.mark.parametrize("mode,expected_errno,expected_data", [
    ("excl", "7", b"original"),  # wire ERRNO_EXIST
    ("keep", "0", b"original"),
    ("trunc", "0", b""),
    ("legacy", "0", b""),
])
def test_create_flags_on_server(mounted_share, mode, expected_errno, expected_data):
    # Drive CREATE directly: one client's kernel usually rejects O_EXCL for
    # an existing path before it reaches the server. Other clients can race
    # with that check, so the server must enforce it atomically too.
    served, _, endpoint = mounted_share
    target = served / "target"
    target.write_bytes(b"original")
    result = run_rawclient(endpoint, "create", "/target", mode)
    assert result["_retval"] == 0
    assert result["ERRNO"] == expected_errno
    assert target.read_bytes() == expected_data

    result = run_rawclient(endpoint, "create", "/new", mode)
    assert result["_retval"] == 0
    assert result["ERRNO"] == "0"
    assert (served / "new").is_file()


def test_exclusive_create_across_clients(mounted_share):
    served, mount, _ = mounted_share
    directories = [mount("client-a"), mount("client-b")]
    barrier = threading.Barrier(2)

    def create(directory, name):
        barrier.wait(timeout=5)
        try:
            fd = os.open(directory / name, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        except FileExistsError:
            return False
        try:
            os.write(fd, b"winner")
        finally:
            os.close(fd)
        return True

    with ThreadPoolExecutor(max_workers=2) as pool:
        for i in range(100):
            name = f"exclusive-{i}"
            attempts = [pool.submit(create, directory, name) for directory in directories]
            assert sum(attempt.result() for attempt in attempts) == 1
            assert (served / name).read_bytes() == b"winner"


@pytest.mark.parametrize("dangling", [False, True])
def test_exclusive_create_does_not_follow_symlink(mounted_share, tmp_path, dangling):
    served, _, endpoint = mounted_share
    outside = tmp_path / "outside"
    if not dangling:
        outside.write_bytes(b"untouched")
    (served / "link").symlink_to(outside)

    result = run_rawclient(endpoint, "create", "/link", "excl")

    assert result["_retval"] == 0
    assert result["ERRNO"] == "7"  # EEXIST, even for a dangling symlink
    assert (served / "link").is_symlink()
    if dangling:
        assert not outside.exists()
    else:
        assert outside.read_bytes() == b"untouched"


def test_hard_link_counts(mounted_share):
    served, mount, _ = mounted_share
    directory = mount("client")
    source = directory / "source"
    alias = directory / "alias"
    source.write_bytes(b"data")
    assert source.stat().st_nlink == 1

    os.link(source, alias)

    assert (served / "source").stat().st_nlink == 2
    assert source.stat().st_nlink == 2
    assert alias.stat().st_nlink == 2
    # readdir prefetches attributes as well; it must not make link counts
    # or sizes stale when another name for the same file changes.
    list(directory.iterdir())
    alias.write_bytes(b"a larger replacement")
    assert source.read_bytes() == b"a larger replacement"
    alias.unlink()
    assert source.stat().st_nlink == 1


@pytest.mark.parametrize("escape", ["parent", "symlink"])
@pytest.mark.parametrize("operation,args", [
    ("getattr", ("{path}",)),
    ("readdir", ("{path}",)),
    ("read", ("{path}", "64", "0")),
    ("rename", ("{path}", "/destination")),
    ("rename", ("/source", "{path}")),
    ("create", ("{path}", "keep")),
])
def test_path_resolution_errors(mounted_share, tmp_path, escape, operation, args):
    served, _, endpoint = mounted_share
    outside = tmp_path / "outside"
    outside.write_bytes(b"secret")
    source = served / "source"
    source.write_bytes(b"source")
    (served / "escape").symlink_to(tmp_path, target_is_directory=True)
    path = "../outside" if escape == "parent" else "/escape/outside"

    result = run_rawclient(endpoint, operation,
                           *(arg.format(path=path) for arg in args))

    assert result["_retval"] == 0
    assert result["ERRNO"] == "5"  # wire ERRNO_ACCES, including CREATE
    assert outside.read_bytes() == b"secret"
    assert source.read_bytes() == b"source"
    assert not (served / "destination").exists()


def test_failed_operation_without_errno_reports_io_error(mounted_share):
    served, _, endpoint = mounted_share
    target = served / "target"
    target.write_bytes(b"unchanged")
    # A negative READ size fails a check() without setting a wire errno.
    result = run_rawclient(endpoint, "read", "/target", "-1", "0")
    assert result["_retval"] == 0
    assert result["ERRNO"] == "1"  # ERRNO_UNKNOWN maps to EIO
    assert result["SIZE"] == "-1"
    assert target.read_bytes() == b"unchanged"
