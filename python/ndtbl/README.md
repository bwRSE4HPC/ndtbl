# ndtbl

[![License](https://img.shields.io/pypi/l/ndtbl?label=License)](https://opensource.org/licenses/MIT)
[![codecov](https://codecov.io/gh/thomasisensee/ndtbl/graph/badge.svg?flag=python&token=5N4GQ0YP7I)](https://codecov.io/gh/thomasisensee/ndtbl)
[![pre-commit.ci](https://results.pre-commit.ci/badge/github/bwrse4hpc/ndtbl/main.svg)](https://results.pre-commit.ci/latest/github/bwrse4hpc/ndtbl/main)
[![PyPI](https://img.shields.io/pypi/v/ndtbl?logo=pypi&logoColor=gold&label=PyPI)](https://pypi.org/project/ndtbl)
[![Python](https://img.shields.io/pypi/pyversions/ndtbl?logo=python&logoColor=gold&label=Python)](https://pypi.org/project/ndtbl)

Pure-Python tools for reading, writing, inspecting, and generating `.ndtbl` files without depending on the C++ binaries.

The package is useful when you want to:

- inspect an existing table on a machine without the C++ toolchain
- generate small synthetic tables for tests, examples, and development
- query one concrete grid point from the command line
- read or write `.ndtbl` files directly from Python workflows using NumPy arrays

## 🔧 Features

- Read `.ndtbl` metadata or full payloads
- Write `.ndtbl` files compatible with the current C++ implementation
- Inspect files from the command line with metadata and sample values
- Query one point in a table by zero-based grid indices
- Generate small predefined linear tables for development workflows

## ⚙️ Installation

Install from the package directory:

```bash
python -m pip install .
```

Or install from PyPI:

```bash
python -m pip install ndtbl
```

For development:

```bash
python -m pip install -v -e .[lint,tests]
```

## 🐍 Python API

The core API revolves around `FieldGroup`, `UniformAxis`, `ExplicitAxis`, `read_group`, and `write_group`.

```python
import numpy as np

from ndtbl import FieldGroup, UniformAxis, ExplicitAxis, read_group, write_group

group = FieldGroup(
    axes=(ExplicitAxis([0.0, 0.1, 1.0]), UniformAxis(10.0, 20.0, 2)),
    field_names=("A", "B"),
    values=np.array(
        [
            [[1.0, 10.0], [2.0, 20.0]],
            [[3.0, 30.0], [4.0, 40.0]],
            [[5.0, 50.0], [6.0, 60.0]],
        ],
        dtype=np.float64,
    ),
)

write_group("example.ndtbl", group)

loaded = read_group("example.ndtbl")
print(loaded.field_names)
print(loaded.values[1, 0, :])
```

`write_group` refuses to write files larger than 128 MiB by default. Pass
`max_size_mib=...` when a larger output is intentional.

The library uses standard Python logging and stays silent unless the calling application configures it. Enable ndtbl diagnostics with:

```python
import logging

logging.basicConfig(level=logging.DEBUG)
logging.getLogger("ndtbl").setLevel(logging.DEBUG)
```

Diagnostics use the `ndtbl` logger hierarchy and never include payload values.

The `values` array shape is `axis_0 x axis_1 x ... x field`.

### Demo notebook
Use the [demo notebook](https://github.com/bwrse4hpc/ndtbl/blob/main/python/ndtbl/notebooks/demo.ipynb) to test the Python functionality of `ndtbl` and see how it can be used to read and write binary `.ndtbl` files.


## 💻 CLI

The package installs one executable, `ndtbl`, with three subcommands:

- `inspect` prints metadata and a configurable number of sample values
- `query` prints the field values at one point addressed by zero-based indices
- `generate` creates a simple synthetic table with linear fields

Show the top-level help:

```bash
ndtbl --help
```

Add `--verbose` (or `-v`) before the subcommand to write debug diagnostics to standard error while keeping command results on standard output:

```bash
ndtbl --verbose inspect example.ndtbl
```

### `inspect`

Inspect an existing file:

```bash
ndtbl inspect example.ndtbl
```

Limit the number of printed sample points:

```bash
ndtbl inspect example.ndtbl --samples 3
```

### `query`

Query one point in the table using zero-based indices in axis order:

```bash
ndtbl query example.ndtbl 1 0
```

Print metadata before the queried values:

```bash
ndtbl query --metadata example.ndtbl 1 0
```

For a 3D table, provide three indices:

```bash
ndtbl query example-3d.ndtbl 1 2 0
```

### `generate`

Generate a small linear table:

```bash
ndtbl generate output.ndtbl \
  --axis 0 1 3 \
  --axis 10 20 2 \
  --field-linear A 1.0 2.0 0.0 \
  --field-linear B 5.0 0.0 -1.0
```

Use `float32` output instead of the default `float64`:

```bash
ndtbl generate output.ndtbl \
  --axis 0 1 3 \
  --field-linear A 1.0 2.0 \
  --dtype float32
```

Raise or lower the generation safety limit:

```bash
ndtbl generate output.ndtbl \
  --axis 0 1 100 \
  --axis 0 1 100 \
  --field-linear A 0.0 1.0 1.0 \
  --max-size-mib 32
```

`--field-linear` expects `NAME OFFSET C0 [C1 ...]`, with one coefficient per
axis in storage order.
