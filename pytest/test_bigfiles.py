import os
import hashlib
import random
import pytest
import shutil
import stat
import tempfile
import time
import filecmp

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


def write_random_binary(filename, size):
    with open(filename, "wb") as f:
        f.write(os.urandom(size))


def calc_checksum(filename):
    with open(filename, "rb") as f:
        m = hashlib.sha256()
        m.update(f.read())
        return m.hexdigest()


def test_gigafile():
    basename = "giga.blob"

    filepath_srv = os.path.join(SRV_DIR, basename)
    write_random_binary(filepath_srv, 1024**3)
    checksum_true = calc_checksum(filepath_srv)

    filepath_client = os.path.join(CLIENT_DIR, basename)
    checksum_client = calc_checksum(filepath_client)

    assert checksum_true == checksum_client


def test_giga5file():
    basename = "giga5.blob"

    filepath_srv = os.path.join(SRV_DIR, basename)
    write_random_binary(filepath_srv, 5 * 1024**3)

    filepath_client = os.path.join(CLIENT_DIR, basename)

    # using hashlib occasionally gives failures, so compare files directly
    assert filecmp.cmp(filepath_srv, filepath_client)

