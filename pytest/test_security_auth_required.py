import os
import shutil
import tempfile

from common import run, RHIZOSRV, RHIZOKEYGEN


def test_authorized_keys_file_without_encrypt_is_refused():
    """
    the ZAP handler started for --authorized-keys-file is only consulted
    for CURVE connections. without --encrypt, clients connect with the
    NULL security mechanism and no ZAP domain is set, so libzmq accepts
    all of them without ever asking the handler - the server used to
    start up and serve everyone while looking like it only allowed the
    listed keys.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)
    keyfile = os.path.join(srv_dir, "authorized_keys")

    try:
        # one authorized key, which no client is going to have
        ret = run([RHIZOKEYGEN, os.path.join(srv_dir, "clientkey")])
        assert ret.retval == 0
        shutil.copyfile(os.path.join(srv_dir, "clientkey"), keyfile)

        ret = run([RHIZOSRV, endpoint, srv_dir, "-a", keyfile])

        assert ret.retval != 0, (
            "the server started with --authorized-keys-file but without "
            "--encrypt, in which case the keys file authorizes everyone"
        )
        assert any("--encrypt" in line for line in ret.stderr), (
            f"no diagnostic naming --encrypt was printed: {ret.stderr}"
        )
    finally:
        shutil.rmtree(srv_dir)


def test_unusable_key_file_is_refused():
    """
    the key read from a key file is handed to libzmq as the Z85 text it
    contains. the return values of the socket options were not checked,
    so an unusable key left the socket with CURVE enabled but no secret
    key: the server came up and bound as usual, and every client then
    failed its handshake with nothing logged to say why.
    """
    pwd = os.getcwd()
    endpoint = f"ipc://{pwd}/.rhizo.sock"

    srv_dir = tempfile.mkdtemp(prefix="servedir-", dir=pwd)

    try:
        # a backtick is not part of the Z85 alphabet
        keyfile = os.path.join(srv_dir, "badkey")
        for name in (keyfile, keyfile + ".secret"):
            with open(name, "w") as f:
                f.write("`" * 40)

        ret = run([RHIZOSRV, endpoint, srv_dir, "-e", "-k", keyfile])
        assert ret.retval != 0, (
            "the server started with a key file that does not contain a "
            "usable key, in which case no client can connect to it"
        )

        # a key file holding less than a whole key is just as unusable
        ret = run([RHIZOKEYGEN, os.path.join(srv_dir, "goodkey")])
        assert ret.retval == 0

        shortkey = os.path.join(srv_dir, "shortkey")
        for suffix in ("", ".secret"):
            with open(os.path.join(srv_dir, "goodkey") + suffix, "rb") as f:
                head = f.read(20)
            with open(shortkey + suffix, "wb") as f:
                f.write(head)

        ret = run([RHIZOSRV, endpoint, srv_dir, "-e", "-k", shortkey])
        assert ret.retval != 0, (
            "the server started with a truncated key file"
        )
    finally:
        shutil.rmtree(srv_dir)
