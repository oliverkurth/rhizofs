import os
import shutil
import tempfile

from common import start_server, stop_server, run_rawclient


def _make_dirs():
    pwd = os.getcwd()
    outside_dir = tempfile.mkdtemp(prefix="outside-", dir=pwd)
    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)

    secret = os.path.join(outside_dir, "secret.txt")
    with open(secret, "w") as f:
        f.write("this must not be reachable from the served directory")

    return pwd, srv_dir, outside_dir


def test_symlink_out_of_served_directory_is_not_followed():
    """
    rejecting ".." in a request path is not enough on its own: a symlink
    in the shared directory - planted by a client through SYMLINK, or
    simply unpacked there from an archive - would otherwise let a client
    reach everything the server process can. a raw client is needed
    since the FUSE kernel driver resolves symlinks on the client side
    and would never send such a path.
    """
    pwd, srv_dir, outside_dir = _make_dirs()

    # the same thing a client can create through a SYMLINK request
    os.symlink(outside_dir, os.path.join(srv_dir, "escape"))

    try:
        endpoint = f"ipc://{pwd}/.rhizo.sock"
        start_server(endpoint, srv_dir)

        result = run_rawclient(endpoint, "getattr", "/escape/secret.txt")
        assert result["ATTRS"] == "0", (
            "GETATTR through a symlink leaving the served directory "
            f"returned attributes for a file outside of it: {result}"
        )

        result = run_rawclient(endpoint, "read", "/escape/secret.txt", "128", "0")
        assert result["SIZE"] == "-1", (
            "READ through a symlink leaving the served directory returned "
            f"file data from outside of it: {result}"
        )
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
        shutil.rmtree(outside_dir)


def test_symlink_to_an_absolute_path_is_not_followed():
    """
    the target of a symlink is resolved on whichever side follows it. the
    server must not follow one out of the served directory even when the
    target is an absolute path that exists there.
    """
    pwd, srv_dir, outside_dir = _make_dirs()

    os.symlink("/", os.path.join(srv_dir, "root"))

    try:
        endpoint = f"ipc://{pwd}/.rhizo.sock"
        start_server(endpoint, srv_dir)

        result = run_rawclient(endpoint, "getattr", "/root/etc/passwd")
        assert result["ATTRS"] == "0", (
            "GETATTR through a symlink to the root directory returned "
            f"attributes for a file outside of the share: {result}"
        )
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
        shutil.rmtree(outside_dir)


def test_dangling_symlink_out_of_served_directory_is_not_written_through():
    """
    a symlink whose target does not exist yet still escapes: a write
    creating that target would follow the link out of the served
    directory.
    """
    pwd, srv_dir, outside_dir = _make_dirs()

    target = os.path.join(outside_dir, "created-through-the-link.txt")
    os.symlink(target, os.path.join(srv_dir, "dangling"))

    try:
        endpoint = f"ipc://{pwd}/.rhizo.sock"
        start_server(endpoint, srv_dir)

        # "abcd" as an uncompressed 4 byte datablock
        run_rawclient(endpoint, "writeraw", "/dangling", "4", "61626364")

        assert not os.path.exists(target), (
            "a WRITE through a dangling symlink created a file outside "
            "of the served directory"
        )
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
        shutil.rmtree(outside_dir)


def test_files_in_the_served_directory_are_still_reachable():
    """
    the containment check must not get in the way of ordinary requests.
    """
    pwd, srv_dir, outside_dir = _make_dirs()

    with open(os.path.join(srv_dir, "inside.txt"), "w") as f:
        f.write("reachable")

    # a symlink that stays inside the share keeps working
    os.symlink("inside.txt", os.path.join(srv_dir, "inside-link"))

    try:
        endpoint = f"ipc://{pwd}/.rhizo.sock"
        start_server(endpoint, srv_dir)

        result = run_rawclient(endpoint, "getattr", "/inside.txt")
        assert result["ATTRS"] == "1", f"a file in the share was refused: {result}"

        result = run_rawclient(endpoint, "read", "/inside-link", "128", "0")
        assert result["SIZE"] == "9", (
            f"a symlink staying inside the share was refused: {result}"
        )

        result = run_rawclient(endpoint, "readdir", "/")
        assert int(result["ENTRIES"]) >= 3, (
            f"READDIR of the served directory was refused: {result}"
        )
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
        shutil.rmtree(outside_dir)
