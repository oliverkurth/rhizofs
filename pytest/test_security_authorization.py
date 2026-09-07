import os
import shutil
import tempfile

from common import run, start_server, stop_server, \
                   start_client, stop_client, \
                   RHIZOKEYGEN


def _keygen(path):
    ret = run([RHIZOKEYGEN, path])
    assert ret.retval == 0, f"could not generate a key pair in {path}"
    with open(path, "rt") as f:
        return f.read()


def test_client_key_not_in_the_authorized_keys_file_is_rejected():
    """
    the existing encryption test only covers a client that is let in, so
    nothing checked that a client which is *not* listed in the
    authorized keys file stays out - which is the whole point of the
    file. --encrypt together with --authorized-keys-file is the only
    combination that authenticates clients (see the check in
    src/server/main.c), so this is what has to hold.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    key_dir = tempfile.mkdtemp(prefix="keys-", dir=pwd)
    client_dir = tempfile.mkdtemp(prefix="clientdir-", dir=pwd)

    with open(os.path.join(srv_dir, "inside.txt"), "w") as f:
        f.write("only for authorized clients")

    server_key = os.path.join(key_dir, "serverkey")
    authorized_key = os.path.join(key_dir, "authorized")
    other_key = os.path.join(key_dir, "other")

    _keygen(server_key)
    authorized_pubkey = _keygen(authorized_key)
    _keygen(other_key)

    # only one of the two client keys is authorized
    keys_file = os.path.join(key_dir, "authorized_keys")
    with open(keys_file, "wt") as f:
        f.write(authorized_pubkey)

    try:
        start_server(endpoint, srv_dir,
                     args=["--encrypt", "--keyfile", server_key,
                           "--authorized-keys-file", keys_file])

        # the unauthorized key must not get in
        ret = start_client(endpoint, client_dir,
                           args=[f"--pubkeyfile={server_key}",
                                 f"--clientpubkeyfile={other_key}"],
                           ignore_fail=True)
        assert ret.retval != 0, (
            "the client mounted with a key that is not in the authorized "
            "keys file"
        )
        assert not os.path.ismount(client_dir), (
            "a client using a key that is not in the authorized keys file "
            "ended up with a mounted filesystem"
        )

        # the authorized key still has to work, so that the test above
        # cannot pass just because the setup is broken
        start_client(endpoint, client_dir,
                     args=[f"--pubkeyfile={server_key}",
                           f"--clientpubkeyfile={authorized_key}",
                           "--attr-cache-timeout=0"])

        with open(os.path.join(client_dir, "inside.txt")) as f:
            assert f.read() == "only for authorized clients"
    finally:
        # also unmounts if the client that must not get in did mount,
        # so that a failure here does not leave a mount point behind
        if os.path.ismount(client_dir):
            stop_client(client_dir)
        stop_server()
        shutil.rmtree(srv_dir)
        shutil.rmtree(key_dir)
        shutil.rmtree(client_dir, ignore_errors=True)
