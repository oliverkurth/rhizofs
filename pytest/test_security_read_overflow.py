import os
import tempfile
import shutil


from common import start_server, stop_server, run_rawclient, server_is_alive


def test_read_with_size_beyond_32bit_does_not_corrupt_heap():
    """
    request->size is a 64bit value coming straight from the client. a
    prior bug allocated the read buffer with a 32bit-truncated copy of
    it (calloc(1, (int)request->size)) while still using the full,
    non-truncated value for read()/pread() - a size just over 2**32
    truncates to a tiny allocation while read() is still told to fill
    the full (huge) count, overflowing the heap buffer and crashing
    the server. a raw client is needed since the FUSE kernel driver
    never requests reads anywhere near this size.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)

    filename = "readtarget.bin"
    content = b"A" * 4096
    with open(os.path.join(srv_dir, filename), "wb") as f:
        f.write(content)

    try:
        start_server(endpoint, srv_dir)

        # 2**32 + 16: truncates to 16 when cast to a 32bit int, while
        # (size_t)/(off_t) casts used for the actual read keep the full
        # value.
        oversized = (1 << 32) + 16
        result = run_rawclient(endpoint, "read", filename, str(oversized), "0")

        assert "TIMEOUT" not in result, (
            "server did not respond to an oversized READ request - it "
            f"likely crashed: {result}"
        )
        assert result["SIZE"] == str(len(content)), (
            "READ with an oversized size did not return the real "
            f"(bounded) amount of file data: {result}"
        )
        assert server_is_alive(), "server process died handling the oversized READ"
    finally:
        stop_server()
        shutil.rmtree(srv_dir)


def test_read_with_an_enormous_size_is_served_up_to_a_limit():
    """
    the size of a READ is picked by the client, and the server used to
    hand it to calloc() unchanged. a client could ask any worker thread
    to buffer an arbitrary amount of memory, and a size large enough to
    fail the allocation outright turned into a failed read rather than
    the short read a filesystem is allowed to return.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)

    filename = "readtarget.bin"
    content = b"A" * 4096
    with open(os.path.join(srv_dir, filename), "wb") as f:
        f.write(content)

    try:
        start_server(endpoint, srv_dir)

        # far more than any allocation could satisfy
        result = run_rawclient(endpoint, "read", filename, str(1 << 62), "0")

        assert "TIMEOUT" not in result, (
            f"server did not respond to a huge READ request: {result}"
        )
        assert result["SIZE"] == str(len(content)), (
            "READ with a size beyond any possible allocation did not "
            f"return the file's data: {result}"
        )
        assert server_is_alive(), "server process died handling a huge READ"
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
