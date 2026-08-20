"""Create, inspect, and validate multidimensional ndtbl files."""

from importlib import metadata

from .io import read_group, read_metadata, write_group
from .model import (
    ExplicitAxis,
    FieldGroup,
    GroupMetadata,
    NdtblFormatError,
    UniformAxis,
)

try:
    __version__ = metadata.version("ndtbl")
except metadata.PackageNotFoundError:
    __version__ = "0.0.0"

__all__ = [
    "ExplicitAxis",
    "FieldGroup",
    "GroupMetadata",
    "NdtblFormatError",
    "UniformAxis",
    "__version__",
    "read_group",
    "read_metadata",
    "write_group",
]
