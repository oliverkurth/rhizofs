import os
import tempfile
import shutil


from common import start_server, stop_server, run_rawclient


def test_getattr_path_traversal_is_rejected():
    """
    a request path containing ".." must not be able to escape the
    directory the server was told to serve. a raw client is used
    here since the FUSE kernel driver would normally resolve ".."
    components itself before rhizofs ever sees the path - only a
    client speaking the wire protocol directly can send one.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    outside_dir = tempfile.mkdtemp(prefix="outside-", dir=pwd)
    secret_name = "secret.txt"
    secret_path = os.path.join(outside_dir, secret_name)
    secret_content = b"this must not be reachable from the served directory"
    with open(secret_path, "wb") as f:
        f.write(secret_content)

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)

    try:
        start_server(endpoint, srv_dir)

        traversal_path = f"../{os.path.basename(outside_dir)}/{secret_name}"
        result = run_rawclient(endpoint, "getattr", traversal_path)

        assert result["ATTRS"] == "0", (
            "GETATTR with a path escaping the served directory returned "
            f"attributes for a file outside of it: {result}"
        )
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
        shutil.rmtree(outside_dir)


def test_rename_path_to_traversal_is_rejected():
    """
    the "path_to" of a RENAME request is also client-controlled and
    must be confined to the served directory, just like "path".
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    outside_dir = tempfile.mkdtemp(prefix="outside-", dir=pwd)
    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)

    source_name = "source.txt"
    with open(os.path.join(srv_dir, source_name), "wb") as f:
        f.write(b"should stay inside the served directory")

    target_name = "escaped.txt"
    outside_target = os.path.join(outside_dir, target_name)

    try:
        start_server(endpoint, srv_dir)

        traversal_path_to = f"../{os.path.basename(outside_dir)}/{target_name}"
        run_rawclient(endpoint, "rename", source_name, traversal_path_to)

        assert not os.path.exists(outside_target), (
            "rename with a path_to escaping the served directory created "
            f"a file outside of it: {outside_target}"
        )
    finally:
        stop_server()
        shutil.rmtree(srv_dir)
        shutil.rmtree(outside_dir)
