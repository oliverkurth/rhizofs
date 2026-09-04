import os
import tempfile
import shutil


from common import start_server, stop_server, run_rawclient, server_is_alive


def test_write_with_truncated_lz4_block_does_not_crash_server():
    """
    a WRITE request carries a DataBlock with a claimed (uncompressed)
    "size" and the actual compressed bytes. the decompression used to
    call LZ4_uncompress(), which trusts the source buffer to actually
    contain enough compressed data to produce "size" bytes of output
    and reads past its end (and, on an old, unrelated bug in the
    surrounding error handling, went on to a double free) if it does
    not. a client can freely declare any "size" while sending far
    fewer actual (malformed) compressed bytes, so this can only be
    exercised with a raw client - the real client always sends a
    consistent, correctly compressed block.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    filename = "lz4test.bin"

    try:
        start_server(endpoint, srv_dir)

        # a single LZ4 token byte (literal length 0, match length 0)
        # claiming an uncompressed size of 16 bytes: there is no way to
        # produce 16 bytes of output from 1 byte of compressed input, so
        # this must be rejected rather than read past the end of the
        # 1-byte source buffer.
        result = run_rawclient(endpoint, "writeraw", filename, "16", "00")

        assert "TIMEOUT" not in result, (
            "server did not respond to a malformed/truncated LZ4 WRITE "
            f"request - it likely crashed: {result}"
        )
        assert result["HASSIZE"] == "0", (
            f"WRITE with a truncated LZ4 block was not rejected: {result}"
        )
        assert server_is_alive(), (
            "server process died handling the malformed LZ4 WRITE request"
        )

        written_path = os.path.join(srv_dir, filename)
        actual_size = os.path.getsize(written_path) if os.path.exists(written_path) else 0
        assert actual_size == 0, (
            "a rejected LZ4 WRITE still resulted in data being written to disk"
        )
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
