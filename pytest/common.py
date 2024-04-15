import os
import signal
import shutil
import stat
import subprocess
import time


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
    print(f"starting process: {cmd}")
    process = subprocess.Popen(cmd, shell=False,  # nosec
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE,
                               text=True)
    out, err = process.communicate()

    stdout = out.strip()
    stderr = err.strip()
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
    print(ret.retval)

    assert ret.retval == 0

    return ret


def start_server_fg(endpoint, directory, args=[]):

    cmd = [RHIZOSRV, endpoint, directory] + args + ["-f"]
    print("starting server in foreground:", " ".join(cmd))
    return subprocess.Popen(cmd, text=True)


def stop_server_fg(process):
    process.terminate()
    process.wait()


def start_client(endpoint, directory, args=[], ignore_fail=False):
    ret = run([RHIZOFS] + args + [endpoint, directory])
    print(ret.stderr)
    print(ret.stdout)
    print(ret.retval)

    if not ignore_fail:
        assert ret.retval == 0

    return ret


def stop_client(directory):
    if shutil.which("fusermount"):
        ret = run(["fusermount", "-u", directory])
    else:
        # no fusermount on MacOS
        ret = run(["umount", directory])
        if ret.retval != 0:
            # fails sometimes, works if we wait a sec
            time.sleep(1)
            ret = run(["umount", directory])

    print (f"retval={ret.retval}")
    assert ret.retval == 0


def vmci_supported():
    try:
        return stat.S_ISCHR(os.stat("/dev/vmci").st_mode)
    except FileNotFoundError:
        return False
