import os
import pytest
import shutil
import tempfile
import time


from common import start_server, stop_server, \
                   start_client, stop_client, \
                   run, \
                   vmci_supported, \
                   RHIZOSRV, RHIZOFS, RHIZOKEYGEN


def test_start_stop_server():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"
    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir)

    time.sleep(1)

    stop_server()
    shutil.rmtree(srv_dir)


def test_start_stop_server_localhost():
    pwd = os.getcwd()
    # github workflow needs IP, "localhost" does not work
    endpoint = "tcp://127.0.0.1:9999"
    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir)

    time.sleep(1)

    stop_server()
    shutil.rmtree(srv_dir)


@pytest.mark.skipif(not vmci_supported(), reason="VMCI not supported")
def test_start_stop_server_vmci():
    pwd = os.getcwd()
    endpoint = "vmci://1:9999"
    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir)

    time.sleep(1)

    stop_server()
    shutil.rmtree(srv_dir)


def test_start_stop_server_encrypted():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"
    pubkey_file = os.path.join(pwd, "rhizo-key.pub")
    if os.path.exists(pubkey_file):
        os.remove(pubkey_file)
    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir, args=["--encrypt", "--pubkeyfile", pubkey_file])
    assert os.path.exists(pubkey_file)

    time.sleep(1)

    stop_server()
    shutil.rmtree(srv_dir)


def test_mount():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir)

    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)
    start_client(endpoint, client_dir)

    time.sleep(1)

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


@pytest.mark.skipif(not vmci_supported(), reason="VMCI not supported")
def test_mount_vmci():
    pwd = os.getcwd()
    endpoint = "vmci://1:9999"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir)

    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)
    start_client(endpoint, client_dir)

    time.sleep(1)

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


def test_mount_keyfile():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"
    pubkey_file = os.path.join(pwd, "rhizo-key.pub")
    key_file = os.path.join(pwd, "rhizo-server-key")

    run([RHIZOKEYGEN, key_file])
    assert os.path.exists(key_file)
    assert os.path.exists(f"{key_file}.secret")

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir, args=["--encrypt", "--keyfile", key_file])

    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)
    start_client(endpoint, client_dir, args=[f"--pubkeyfile={key_file}"])

    time.sleep(1)

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


def test_mount_pubkeyfile():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"
    pubkey_file = os.path.join(pwd, "rhizo-key.pub")

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir, args=["--encrypt", "--pubkeyfile", pubkey_file])

    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)
    start_client(endpoint, client_dir, args=[f"--pubkeyfile={pubkey_file}"])

    time.sleep(1)

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


def test_mount_pubkeyfile_client_keyfile():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"
    pubkey_file = os.path.join(pwd, "rhizo-key.pub")
    client_key_file = os.path.join(pwd, "rhizo-client-key")

    run([RHIZOKEYGEN, client_key_file])
    assert os.path.exists(client_key_file)
    assert os.path.exists(f"{client_key_file}.secret")

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir, args=["--encrypt", "--pubkeyfile", pubkey_file])

    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)
    start_client(endpoint, client_dir, args=[f"--pubkeyfile={pubkey_file}", f"--clientpubkeyfile={client_key_file}"])

    time.sleep(1)

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


def test_mount_zap():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"
    pubkey_file = os.path.join(pwd, "rhizo-key.pub")
    client_key_file = os.path.join(pwd, "rhizo-client-key")
    authorized_keys_file = os.path.join(pwd, "authorized_keys")

    run([RHIZOKEYGEN, client_key_file])
    assert os.path.exists(client_key_file)
    assert os.path.exists(f"{client_key_file}.secret")

    shutil.copyfile(client_key_file, authorized_keys_file)

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir, args=["--encrypt", "--pubkeyfile", pubkey_file, "-a", authorized_keys_file])

    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)
    start_client(endpoint, client_dir, args=[f"--pubkeyfile={pubkey_file}", f"--clientpubkeyfile={client_key_file}"])

    time.sleep(1)

    # make sure we can do something with it
    basename = "readdir.txt"
    filename = os.path.join(client_dir, basename)
    with open(filename, "w") as f:
        f.write("bla")

    entries = os.listdir(client_dir)
    assert basename in entries

    stop_client(client_dir)
    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)


def test_mount_zap_invalid():
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"
    pubkey_file = os.path.join(pwd, "rhizo-key.pub")
    client_key_file = os.path.join(pwd, "rhizo-client-key")
    authorized_keys_file = os.path.join(pwd, "authorized_keys")

    run([RHIZOKEYGEN, client_key_file])
    assert os.path.exists(client_key_file)
    assert os.path.exists(f"{client_key_file}.secret")

    with open(authorized_keys_file, "wt") as f:
        f.write("totallywrongkey")

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    ret = start_server(endpoint, srv_dir, args=["--encrypt", "--pubkeyfile", pubkey_file, "-a", authorized_keys_file])

    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)
    ret = start_client(endpoint, client_dir, args=[f"--pubkeyfile={pubkey_file}", f"--clientpubkeyfile={client_key_file}"], ignore_fail=True)
    assert ret.retval != 0

    shutil.rmtree(client_dir)

    stop_server()
    shutil.rmtree(srv_dir)
