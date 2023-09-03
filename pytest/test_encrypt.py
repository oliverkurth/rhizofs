import os
import pytest
import shutil
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
    pubkey_file = os.path.join(pwd, "rhizo-key.pub")

    srv_dir = SRV_DIR
    os.makedirs(srv_dir, exist_ok=True)

    if os.path.exists(pubkey_file):
        os.remove(pubkey_file)
    ret = start_server(endpoint, srv_dir, args=["--encrypt", "--pubkeyfile", pubkey_file])
    print (ret.stdout)
    print (ret.stderr)

    with open(pubkey_file, "rt") as f:
        pubkey = f.read()

    client_dir = CLIENT_DIR
    os.makedirs(client_dir, exist_ok=True)
    start_client(endpoint, client_dir, args = [f"--pubkeyfile={pubkey_file}"])

    time.sleep(1)

    yield

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


def test_touch_file_on_srvdir():
    basename = "touched-on-srv.txt"

    filename = os.path.join(SRV_DIR, basename)
    with open(filename, "w") as f:
        f.write("something\n")

    filename = os.path.join(CLIENT_DIR, basename)
    assert os.path.exists(filename)


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
