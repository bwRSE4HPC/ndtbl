# SPDX-FileCopyrightText: 2026 Thomas Isensee
# SPDX-License-Identifier: MIT

"""Check the Python writer / C++ reader example in a temporary directory."""

import io
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np
from numpy.polynomial import Polynomial
from numpy.testing import assert_allclose


def main() -> None:
    """Run the unchanged example programs and check their numerical output."""
    executable = Path(sys.argv[1]).resolve()
    generator = Path(__file__).with_name("generate.py")
    with tempfile.TemporaryDirectory(prefix="ndtbl-example-") as directory:
        subprocess.run([sys.executable, str(generator)], cwd=directory, check=True)
        result = subprocess.run(
            [str(executable)],
            cwd=directory,
            check=True,
            capture_output=True,
            text=True,
        )

    output = np.loadtxt(io.StringIO(result.stdout))
    if output.shape != (101, 3):
        raise AssertionError(f"Expected 101 rows and 3 columns, got {output.shape}")
    if not np.isfinite(output).all():
        raise AssertionError("Example output contains non-finite values")

    coordinates = np.linspace(0.0, 1.0, 101)
    nodes = np.linspace(0.0, 1.0, 11)
    # Independent reference samples of the function used in the paper.
    samples = (
        np.exp(-80.0 * (nodes - 0.4) ** 2)
        + 0.4 * np.exp(-120.0 * (nodes - 0.7) ** 2)
        + 0.1 * np.sin(8 * np.pi * nodes)
    )
    assert_allclose(output[:, 0], coordinates, rtol=0, atol=1e-14)

    # The illustrative C++ program prints six significant digits by default.
    tolerance = {"rtol": 5e-6, "atol": 1e-12}
    assert_allclose(output[:, 1], np.interp(coordinates, nodes, samples), **tolerance)
    assert_allclose(output[::10, 2], samples, **tolerance)

    # Independently fit cubics at an interior query and near both boundaries.
    # Off-grid values should match interpolation, not the analytic function.
    for row, first_node in ((5, 0), (45, 3), (95, 7)):
        support = slice(first_node, first_node + 4)
        cubic = Polynomial.fit(nodes[support], samples[support], deg=3)
        assert_allclose(output[row, 2], cubic(coordinates[row]), **tolerance)


if __name__ == "__main__":
    main()
