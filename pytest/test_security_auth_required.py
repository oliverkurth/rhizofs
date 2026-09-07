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
