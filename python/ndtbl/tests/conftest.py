# SPDX-FileCopyrightText: 2026 Thomas Isensee
# SPDX-License-Identifier: MIT

import numpy as np
import pytest
from click.testing import CliRunner

from ndtbl import ExplicitAxis, FieldGroup, UniformAxis


@pytest.fixture
def runner() -> CliRunner:
    return CliRunner()


@pytest.fixture
def sample_uniform_group() -> FieldGroup:
    return FieldGroup(
        axes=(UniformAxis(0.0, 1.0, 2), UniformAxis(10.0, 20.0, 3)),
        field_names=("A", "B"),
        values=np.arange(12, dtype=np.float64).reshape(2, 3, 2),
    )


@pytest.fixture
def sample_explicit_group() -> FieldGroup:
    return FieldGroup(
        axes=(ExplicitAxis([0.0, 0.5, 1.5]), UniformAxis(2.0, 4.0, 2)),
        field_names=("C",),
        values=np.array(
            [
                [[0.0], [1.0]],
                [[2.0], [3.0]],
                [[4.0], [5.0]],
            ],
            dtype=np.float32,
        ),
    )
