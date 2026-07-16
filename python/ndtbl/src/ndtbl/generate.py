from dataclasses import dataclass
from typing import cast

import numpy as np

from .model import (
    FieldGroup,
    FloatArray,
    FloatDType,
    FloatDTypeLike,
    UniformAxis,
    normalize_dtype,
)

FILE_MAGIC_SIZE = 8
HEADER_PREFIX_SIZE = 1 + 1 + 2 + 8
DIMENSION_HEADER_SIZE = 8 * 3
AXIS_HEADER_SIZE = 8 + 8
UNIFORM_AXIS_PAYLOAD_SIZE = 16
STRING_LENGTH_SIZE = 8


@dataclass(frozen=True, slots=True)
class GenerationSizeEstimate:
    """Summary of the expected size of a generated table.

    Args:
        point_count: Total number of grid points in the generated table.
        field_count: Number of generated fields.
        payload_bytes: Size of the numeric payload in bytes.
        estimated_file_bytes: Approximate full file size in bytes.
    """

    point_count: int
    field_count: int
    payload_bytes: int
    estimated_file_bytes: int

    @property
    def estimated_file_mib(self) -> float:
        """Return the estimated file size in mebibytes."""
        return self.estimated_file_bytes / (1024 * 1024)


@dataclass(frozen=True, slots=True)
class LinearFieldSpec:
    """Definition of a generated linear field.

    Args:
        name: Field name written into the output table.
        offset: Constant term added to every point.
        coefficients: One coefficient per axis, in axis order.
    """

    name: str
    offset: float
    coefficients: tuple[float, ...]

    def __post_init__(self) -> None:
        """Normalize field definition values after initialization."""
        coefficients = tuple(float(value) for value in self.coefficients)
        if not coefficients:
            raise ValueError("linear fields require at least one coefficient")
        object.__setattr__(self, "name", str(self.name))
        object.__setattr__(self, "offset", float(self.offset))
        object.__setattr__(self, "coefficients", coefficients)


def _linear_field(
    axes: tuple[UniformAxis, ...],
    spec: LinearFieldSpec,
    dtype: FloatDType,
) -> FloatArray:
    """Evaluate one linear field over a uniform grid.

    Args:
        axes: Uniform axes that define the grid.
        spec: Linear field definition to evaluate.
        dtype: Output dtype for the generated values.

    Returns:
        Array of field values shaped like the grid.
    """

    if len(spec.coefficients) != len(axes):
        raise ValueError(
            "linear field coefficient count does not match axis count"
        )

    axis_sizes = tuple(axis.size for axis in axes)
    field = cast(FloatArray, np.full(axis_sizes, spec.offset, dtype=dtype))

    for axis_index, coefficient in enumerate(spec.coefficients):
        coordinates = cast(
            FloatArray,
            axes[axis_index].coordinates().astype(dtype, copy=False),
        )
        broadcast_shape = [1] * len(axes)
        broadcast_shape[axis_index] = axes[axis_index].size
        field += coefficient * coordinates.reshape(broadcast_shape)

    return field


GENERATOR_REGISTRY = {
    "linear": _linear_field,
}


def generate_group(
    axes: tuple[UniformAxis, ...],
    field_specs: tuple[LinearFieldSpec, ...],
    dtype: FloatDTypeLike = np.float64,
) -> FieldGroup:
    """Generate an in-memory field group from linear field specifications.

    Args:
        axes: Uniform axes that define the generated grid.
        field_specs: Linear field definitions in output field order.
        dtype: Payload dtype for the generated values.

    Returns:
        A populated field group containing the generated values.
    """

    if not axes:
        raise ValueError("at least one axis is required for generation")
    if not field_specs:
        raise ValueError("at least one field is required for generation")

    normalized_dtype = normalize_dtype(dtype)
    axis_sizes = tuple(axis.size for axis in axes)
    values = np.empty((*axis_sizes, len(field_specs)), dtype=normalized_dtype)

    for field_index, spec in enumerate(field_specs):
        generator = GENERATOR_REGISTRY["linear"]
        values[..., field_index] = generator(axes, spec, normalized_dtype)

    field_names = tuple(spec.name for spec in field_specs)
    return FieldGroup(axes=axes, field_names=field_names, values=values)


def estimate_generated_group_size(
    axes: tuple[UniformAxis, ...],
    field_specs: tuple[LinearFieldSpec, ...],
    dtype: FloatDTypeLike = np.float64,
) -> GenerationSizeEstimate:
    """Estimate the size of a generated ndtbl file.

    Args:
        axes: Uniform axes that define the generated grid.
        field_specs: Linear field definitions in output field order.
        dtype: Payload dtype for the generated values.

    Returns:
        Summary including point count, payload size, and file size estimate.
    """

    if not axes:
        raise ValueError("at least one axis is required for generation")
    if not field_specs:
        raise ValueError("at least one field is required for generation")

    normalized_dtype = normalize_dtype(dtype)
    point_count = 1
    for axis in axes:
        point_count *= axis.size

    field_count = len(field_specs)
    payload_bytes = point_count * field_count * normalized_dtype.itemsize

    metadata_bytes = (
        FILE_MAGIC_SIZE + HEADER_PREFIX_SIZE + DIMENSION_HEADER_SIZE
    )
    axis_bytes = len(axes) * (AXIS_HEADER_SIZE + UNIFORM_AXIS_PAYLOAD_SIZE)
    field_name_bytes = sum(
        STRING_LENGTH_SIZE + len(spec.name.encode("utf-8"))
        for spec in field_specs
    )

    return GenerationSizeEstimate(
        point_count=point_count,
        field_count=field_count,
        payload_bytes=payload_bytes,
        estimated_file_bytes=metadata_bytes
        + axis_bytes
        + field_name_bytes
        + payload_bytes,
    )
