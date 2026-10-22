import os
import subprocess

from common import BINDIR


def test_resolver_errors_and_chmod_compatibility():
    subprocess.run([os.path.join(BINDIR, "rhizo-server-unit")],
                   check=True, timeout=10)
