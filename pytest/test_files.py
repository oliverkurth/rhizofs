import os
import random
import pytest
import shutil
import stat
import tempfile
import time


from common import start_server, stop_server, \
                   start_client, stop_client, \
                   RHIZOSRV, RHIZOFS


SRV_DIR=os.path.join(os.getcwd(), "srvdir-files")
CLIENT_DIR=os.path.join(os.getcwd(), "clientdir-files")


@pytest.fixture(scope='module', autouse=True)
def setup_test():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = SRV_DIR
    os.makedirs(srv_dir, exist_ok=True)
    ret = start_server(endpoint, srv_dir)

    client_dir = CLIENT_DIR
    os.makedirs(client_dir, exist_ok=True)
    start_client(endpoint, client_dir)

    time.sleep(1)

    yield

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


def write_file(filename, text):
    with open(filename, "wt") as f:
        f.write(text)


def test_write_file_on_srvdir():
    basename = "created-on-srv.txt"
    text = "something from server\n"

    filename = os.path.join(SRV_DIR, basename)
    with open(filename, "wt") as f:
        f.write(text)

    filename = os.path.join(CLIENT_DIR, basename)
    assert os.path.exists(filename)

    with open(filename, "rt") as f:
        content = f.read()

    assert content == text


def test_write_file_on_clientdir():
    basename = "created-on-client.txt"
    text = "something from client"

    filename = os.path.join(CLIENT_DIR, basename)
    with open(filename, "w") as f:
        f.write(text)

    filename = os.path.join(SRV_DIR, basename)
    assert os.path.exists(filename)

    with open(filename, "rt") as f:
        content = f.read()

    assert content == text


def test_readdir():
    basename = "readdir.txt"
    text = "something from client"

    filename = os.path.join(CLIENT_DIR, basename)
    with open(filename, "w") as f:
        f.write(text)

    entries = os.listdir(CLIENT_DIR)
    assert basename in entries


def test_attributes():
    basename = "attributes.txt"
    text = "something from client"
    now = time.time()

    filename = os.path.join(CLIENT_DIR, basename)
    with open(filename, "w") as f:
        f.write(text)

    stat = os.stat(filename)
    assert stat.st_uid == os.getuid()
    assert stat.st_mtime >= int(now)


def test_rmdir():
    dirname = "rmdir-dir"
    dirpath = os.path.join(CLIENT_DIR, dirname)

    os.makedirs(dirpath)
    assert os.path.isdir(dirpath)

    os.rmdir(dirpath)
    assert not os.path.exists(dirpath)


def test_unlink():
    basename = "unlink.txt"
    text = "something from client"
    now = time.time()

    filename = os.path.join(CLIENT_DIR, basename)
    with open(filename, "w") as f:
        f.write(text)
    assert os.path.exists(filename)

    os.remove(filename)
    assert not os.path.exists(filename)


def test_access():
    now = time.time()

    filename = os.path.join(CLIENT_DIR, "access.txt")
    write_file(filename, "something from client")

    assert os.access(filename, os.F_OK)


def test_truncate():
    size = 10
    filename = os.path.join(CLIENT_DIR, "truncate.txt")
    write_file(filename, "something from client")

    os.truncate(filename, size)
    stat = os.stat(filename)
    assert stat.st_size == size


def test_chmod():
    filename = os.path.join(CLIENT_DIR, "chmod.txt")
    write_file(filename, "something from client")

    mode = 0o600
    os.chmod(filename, mode)
    s = os.stat(filename)
    assert stat.S_IMODE(s.st_mode) == mode

    mode = 0o644
    os.chmod(filename, mode)
    s = os.stat(filename)
    assert stat.S_IMODE(s.st_mode) == mode


def test_utimens():
    filename = os.path.join(CLIENT_DIR, "utimens.txt")
    write_file(filename, "something from client")

    now = int(time.time())
    atime = now - random.randrange(1, 86400)
    mtime = now - random.randrange(1, 86400)
    os.utime(filename, times = (atime, mtime))
    s = os.stat(filename)
    assert s.st_atime == atime
    assert s.st_mtime == mtime


def test_link():
    filename = os.path.join(CLIENT_DIR, "linked.txt")
    linkname = os.path.join(CLIENT_DIR, "link.txt")
    write_file(filename, "something from client")

    os.link(filename, linkname)
    assert os.path.exists(filename)
    assert os.path.exists(linkname)

    s_file = os.stat(filename)
    s_link = os.stat(linkname)

    # TODO: inodes are different on client,
    # but we can check on the server
#    assert s_file.st_ino == s_link.st_ino


def test_rename():
    oldname = os.path.join(CLIENT_DIR, "rename.txt")
    newname = os.path.join(CLIENT_DIR, "renamed.txt")
    write_file(oldname, "something from client")

    os.rename(oldname, newname)
    assert not os.path.exists(oldname)
    assert os.path.exists(newname)


def test_symlink():
    filename = os.path.join(CLIENT_DIR, "symlinked.txt")
    linkname = os.path.join(CLIENT_DIR, "symlink.txt")
    write_file(filename, "something from client")

    os.symlink(filename, linkname)
    assert os.path.exists(filename)
    assert os.path.exists(linkname)
    assert os.path.islink(linkname)
    
    dst = os.readlink(linkname)
    assert filename == dst


def test_statfs():
    filename = os.path.join(CLIENT_DIR, "statvfs.txt")
    write_file(filename, "something from client")
    os.statvfs(filename)

