import multiprocessing
import os
import shutil
import tempfile

from common import start_server, stop_server, run_rawclient, server_is_alive


ENTRY_COUNT = 4000
READDIR_PASSES = 12
MAX_CREATED = 60000


def _create_files(directory, stop_flag):
    """
    keep adding entries to the directory while the server is scanning it.
    a second client issuing WRITE requests (which open with O_CREAT) has
    exactly the same effect through the wire protocol.
    """
    i = 0
    while not stop_flag.is_set() and i < MAX_CREATED:
        try:
            path = os.path.join(directory, "new%06d" % i)
            open(path, "w").close()
            # keep the directory from growing without bound over the run
            # while still adding and removing entries underneath the
            # server's two scanning passes
            if i % 2:
                os.unlink(path)
        except OSError:
            break
        i += 1


def test_readdir_of_a_growing_directory_does_not_corrupt_the_heap():
    """
    ServeDir_op_readdir() sizes the response array from a first pass over
    the directory and then fills it in a second pass. entries created
    between the two passes used to make the second pass write attribute
    pointers past the end of the array - remotely triggerable heap
    corruption, since the server is multithreaded and one client's
    READDIR races another's WRITE.

    this is a stress test: it cannot force the race deterministically, so
    it hammers a large directory while it grows and checks that the
    server keeps answering. an AddressSanitizer build reports the
    overflow on the first pass that hits the race.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    for i in range(ENTRY_COUNT):
        open(os.path.join(srv_dir, "f%05d" % i), "w").close()

    stop_flag = multiprocessing.Event()
    creator = multiprocessing.Process(target=_create_files,
                                      args=(srv_dir, stop_flag))

    try:
        start_server(endpoint, srv_dir)

        creator.start()
        for _ in range(READDIR_PASSES):
            result = run_rawclient(endpoint, "readdir", "/")

            assert "TIMEOUT" not in result, (
                "server stopped answering READDIR while the directory was "
                f"growing - it likely crashed: {result}"
            )
            assert int(result["ENTRIES"]) >= ENTRY_COUNT, (
                "READDIR of a growing directory returned fewer entries "
                f"than the directory had to begin with: {result}"
            )

        stop_flag.set()
        creator.join(timeout=30)

        assert server_is_alive(), (
            "server process died while serving READDIR for a growing "
            "directory"
        )
    finally:
        stop_flag.set()
        if creator.is_alive():
            creator.terminate()
        creator.join(timeout=30)
        stop_server()
        shutil.rmtree(srv_dir)
