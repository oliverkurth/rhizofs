import os
import subprocess

from common import BINDIR


def test_concurrent_cache_operations():
    subprocess.run([os.path.join(BINDIR, "rhizo-attrcache-unit")],
                   check=True, timeout=30)
