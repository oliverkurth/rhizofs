import os
import subprocess

from common import BINDIR


def test_resolver_error_classification():
    subprocess.run([os.path.join(BINDIR, "rhizo-server-unit")],
                   check=True, timeout=10)
