# SPDX-FileCopyrightText: 2026 Thomas Isensee
# SPDX-License-Identifier: MIT

"""Check the Python writer / C++ reader example in a temporary directory."""

import subprocess
import sys
import tempfile
from pathlib import Path


def main() -> None:
    """Run the generator and reader, requiring both to exit successfully."""
    executable = Path(sys.argv[1]).resolve()
    generator = Path(__file__).with_name("generate.py")
    with tempfile.TemporaryDirectory(prefix="ndtbl-example-") as directory:
        subprocess.run([sys.executable, str(generator)], cwd=directory, check=True)
        subprocess.run([str(executable)], cwd=directory, check=True)


if __name__ == "__main__":
    main()
