# SPDX-FileCopyrightText: 2026 Thomas Isensee
# SPDX-License-Identifier: MIT

import ndtbl
import numpy as np


def function(x):
    return (
        np.exp(-80.0 * np.pow(x - 0.4, 2))
        + 0.4 * np.exp(-120 * np.pow(x - 0.7, 2))
        + 0.1 * np.sin(8 * np.pi * x)
    )


axis0 = ndtbl.UniformAxis(min=0.0, max=1.0, size=11)

values_table = function(axis0.coordinates()).astype(np.float64)
values_table = values_table[:, np.newaxis]

group = ndtbl.FieldGroup(
    axes=(axis0,),
    field_names=("A",),
    values=values_table,
)

ndtbl.write_group("example1D.ndtbl", group)
