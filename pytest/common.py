import os
import signal
import stat
import subprocess


BINDIR=os.path.realpath(os.path.join(os.path.dirname(__file__), "..", "bin"))
RHIZOSRV=os.path.join(BINDIR, "rhizosrv")
RHIZOFS=os.path.join(BINDIR, "rhizofs")
RHIZOKEYGEN=os.path.join(BINDIR, "rhizo-keygen")


class CmdReturn:

    def __init__(self, retval, stdout, stderr):
        self.retval = retval
        self.stdout = stdout
        self.stderr = stderr


def run(cmd):
    process = subprocess.Popen(cmd, shell=False,  # nosec
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    out, err = process.communicate()

    stdout = out.decode().strip()
    stderr = err.decode().strip()
    retval = process.returncode

    return CmdReturn(retval, stdout.split('\n'), stderr.split('\n'))


def stop_server():
    pwd = os.getcwd()
    pidfile_path = os.path.join(pwd, "rhizosrv.pid")
    with open(pidfile_path, "r") as f:
        pid = int(f.read())
    os.kill(pid, signal.SIGTERM)


def start_server(endpoint, directory, args=[]):
    pwd = os.getcwd()
    pidfile_path = os.path.join(pwd, "rhizosrv.pid")
    ret = run([RHIZOSRV, endpoint, directory] + args + ["-p", pidfile_path])
    print(ret.stderr)
    print(ret.stdout)
    assert ret.retval == 0

    return ret


def start_client(endpoint, directory, args=[]):
    pwd = os.getcwd()
    ret = run([RHIZOFS] + args + [endpoint, directory])
    assert ret.retval == 0

    return ret


# this won't work on MacOS
def stop_client(directory):
    ret = run(["fusermount", "-u", directory])
    assert ret.retval == 0


def vmci_supported():
    try:
        return stat.S_ISCHR(os.stat("/dev/vmci").st_mode)
    except FileNotFoundError:
        return False
