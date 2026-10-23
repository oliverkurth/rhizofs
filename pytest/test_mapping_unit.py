import os
import subprocess

from common import BINDIR


def test_wire_link_counts():
    subprocess.run([os.path.join(BINDIR, "rhizo-mapping-unit")],
                   check=True, timeout=10)
