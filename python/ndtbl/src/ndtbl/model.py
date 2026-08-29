"""Define the public data model for ndtbl files."""

import math
from dataclasses import dataclass
from typing import TypeAlias

import numpy as np
from numpy.typing import ArrayLike, NDArray

FloatArray: TypeAlias = NDArray[np.float32] | NDArray[np.float64]
FloatDType: TypeAlias = np.dtype[np.float32] | np.dtype[np.float64]
FloatDTypeLike: TypeAlias = (
    FloatDType | type[np.float32] | type[np.float64] | str
)


class NdtblFormatError(RuntimeError):
    """Raised when an .ndtbl file cannot be parsed safely."""


@dataclass(frozen=True, slots=True)
class UniformAxis:
    """Axis with uniformly spaced coordinates.

    Args:
        min: Coordinate value at the first point.
        max: Coordinate value at the last point.
        size: Number of points along the axis.

    """

    min: float
    max: float
    size: int

    def __post_init__(self) -> None:
        """Validate and normalize the axis definition."""
        min_value = float(self.min)
        max_value = float(self.max)
        size = int(self.size)

        if size < 1:
            raise ValueError("uniform axis must contain at least one point")
        if not math.isfinite(min_value) or (
            size > 1 and not math.isfinite(max_value)
        ):
            raise ValueError("uniform axis coordinates must be finite")
        if size > 1 and not max_value > min_value:
            raise ValueError("uniform axis requires max > min when size > 1")
        if size > 1 and not math.isfinite(max_value - min_value):
            raise ValueError("uniform axis span exceeds supported range")
        if size == 1:
            max_value = min_value

        object.__setattr__(self, "min", min_value)
        object.__setattr__(self, "max", max_value)
        object.__setattr__(self, "size", size)

    @property
    def kind(self) -> str:
        """Return the serialized axis kind name."""
        return "uniform"

    def coordinates(self) -> NDArray[np.float64]:
        """Return the axis coordinates as a float64 NumPy array."""
        if self.size == 1:
            return np.asarray([self.min], dtype=np.float64)
        return np.linspace(self.min, self.max, self.size, dtype=np.float64)


@dataclass(frozen=True, slots=True)
class ExplicitAxis:
    """Axis defined by explicitly provided coordinates.

    Args:
        coordinates: One-dimensional, strictly increasing coordinates.

    """

    coordinates_values: tuple[float, ...]

    def __init__(self, coordinates: ArrayLike) -> None:
        """Validate and store explicit coordinate values."""
        raw = np.asarray(coordinates, dtype=np.float64)
        if raw.ndim != 1:
            raise ValueError(
                "explicit axis coordinates must be one-dimensional"
            )
        if raw.size < 1:
            raise ValueError("explicit axis must contain at least one point")
        if not np.all(np.isfinite(raw)):
            raise ValueError("explicit axis coordinates must be finite")
        if raw.size > 1 and np.any(raw[1:] <= raw[:-1]):
            raise ValueError(
                "explicit axis coordinates must be strictly increasing"
            )
        if raw.size > 1 and not math.isfinite(float(raw[-1]) - float(raw[0])):
            raise ValueError("explicit axis span exceeds supported range")
        object.__setattr__(
            self, "coordinates_values", tuple(float(x) for x in raw)
        )

    @property
    def kind(self) -> str:
        """Return the serialized axis kind name."""
        return "explicit"

    @property
    def size(self) -> int:
        """Return the number of points on the axis."""
        return len(self.coordinates_values)

    @property
    def min(self) -> float:
        """Return the first coordinate value."""
        return self.coordinates_values[0]

    @property
    def max(self) -> float:
        """Return the last coordinate value."""
        return self.coordinates_values[-1]

    def coordinates(self) -> NDArray[np.float64]:
        """Return the axis coordinates as a float64 NumPy array."""
        return np.asarray(self.coordinates_values, dtype=np.float64)


Axis: TypeAlias = UniformAxis | ExplicitAxis


def normalize_dtype(
    dtype: FloatDTypeLike,
) -> FloatDType:
    """Normalize supported payload dtypes to canonical NumPy dtypes.

    Args:
        dtype: NumPy dtype object or dtype string to normalize.

    Returns:
        The canonical float32 or float64 dtype object.

    Raises:
        ValueError: If the dtype is not supported by the ndtbl format.

    """
    normalized = np.dtype(dtype)
    if normalized == np.dtype(np.float32):
        return np.dtype(np.float32)
    if normalized == np.dtype(np.float64):
        return np.dtype(np.float64)
    raise ValueError("ndtbl only supports float32 and float64 payloads")


def _normalize_axes(axes: tuple[Axis, ...] | list[Axis]) -> tuple[Axis, ...]:
    """Validate and tuple-normalize a sequence of axes.

    Args:
        axes: Axis objects that define the table dimensions.

    Returns:
        The validated axes as a tuple.

    """
    normalized = tuple(axes)
    if not normalized:
        raise ValueError("field groups must contain at least one axis")
    for axis in normalized:
        if not isinstance(axis, UniformAxis | ExplicitAxis):
            raise TypeError(
                "axes must be UniformAxis or ExplicitAxis instances"
            )
    return normalized


def _normalize_field_names(
    field_names: tuple[str, ...] | list[str],
) -> tuple[str, ...]:
    """Validate and tuple-normalize field names.

    Args:
        field_names: Field names in storage order.

    Returns:
        The validated field names as a tuple of strings.

    """
    normalized = tuple(str(name) for name in field_names)
    if not normalized:
        raise ValueError("field groups must contain at least one field")
    if any(not name for name in normalized):
        raise ValueError("field names must be non-empty strings")
    return normalized


def _normalize_values(values: ArrayLike) -> FloatArray:
    """Convert field values to a contiguous float array.

    Args:
        values: Array-like numeric field payload.

    Returns:
        A contiguous float32 or float64 NumPy array.

    """
    array = np.asarray(values)
    if not np.issubdtype(array.dtype, np.number):
        raise TypeError("field values must be numeric")
    if array.dtype not in (np.dtype(np.float32), np.dtype(np.float64)):
        array = array.astype(np.float64)
    return np.ascontiguousarray(array)


@dataclass(frozen=True, slots=True, init=False)
class GroupMetadata:
    """Metadata describing an ndtbl field group without payload values.

    Args:
        axes: Axes that define the table dimensions.
        field_names: Names of the stored fields.
        dtype: Payload scalar dtype, limited to float32 or float64.
        format_version: ndtbl binary file format version.

    """

    axes: tuple[Axis, ...]
    field_names: tuple[str, ...]
    dtype: FloatDType
    format_version: int = 1

    def __init__(
        self,
        axes: tuple[Axis, ...] | list[Axis],
        field_names: tuple[str, ...] | list[str],
        dtype: FloatDTypeLike,
        format_version: int = 1,
    ) -> None:
        """Normalize metadata fields during initialization."""
        object.__setattr__(self, "axes", _normalize_axes(axes))
        object.__setattr__(
            self, "field_names", _normalize_field_names(field_names)
        )
        object.__setattr__(self, "dtype", normalize_dtype(dtype))
        object.__setattr__(self, "format_version", int(format_version))

    @property
    def dimension(self) -> int:
        """Return the number of axes in the table."""
        return len(self.axes)

    @property
    def field_count(self) -> int:
        """Return the number of fields stored at each point."""
        return len(self.field_names)

    @property
    def axis_sizes(self) -> tuple[int, ...]:
        """Return the point count of each axis in storage order."""
        return tuple(axis.size for axis in self.axes)

    @property
    def point_count(self) -> int:
        """Return the total number of points across all axes."""
        point_count = 1
        for size in self.axis_sizes:
            point_count *= size
        return point_count

    @property
    def dtype_name(self) -> str:
        """Return the payload dtype as a human-readable string."""
        return str(self.dtype)


@dataclass(slots=True, init=False)
class FieldGroup:
    """In-memory ndtbl payload and metadata.

    Args:
        axes: Axes that define the table dimensions.
        field_names: Names of the stored fields.
        values: Array shaped as ``(*axis_sizes, field_count)``.

    """

    axes: tuple[Axis, ...]
    field_names: tuple[str, ...]
    values: FloatArray

    def __init__(
        self,
        axes: tuple[Axis, ...] | list[Axis],
        field_names: tuple[str, ...] | list[str],
        values: ArrayLike,
    ) -> None:
        """Validate and normalize group data during initialization."""
        self.axes = _normalize_axes(axes)
        self.field_names = _normalize_field_names(field_names)
        self.values = _normalize_values(values)

        expected_shape = (*self.axis_sizes, self.field_count)
        if self.values.ndim != self.dimension + 1:
            raise ValueError(
                "field values must have shape (*axis_sizes, field_count)"
            )
        if self.values.shape != expected_shape:
            raise ValueError(
                f"field values shape {self.values.shape} does not match "
                f"expected {expected_shape}"
            )

    @property
    def dimension(self) -> int:
        """Return the number of axes in the group."""
        return len(self.axes)

    @property
    def field_count(self) -> int:
        """Return the number of fields stored at each point."""
        return len(self.field_names)

    @property
    def axis_sizes(self) -> tuple[int, ...]:
        """Return the point count of each axis in storage order."""
        return tuple(axis.size for axis in self.axes)

    @property
    def point_count(self) -> int:
        """Return the total number of points across all axes."""
        point_count = 1
        for size in self.axis_sizes:
            point_count *= size
        return point_count

    @property
    def dtype(self) -> FloatDType:
        """Return the normalized payload dtype."""
        return normalize_dtype(self.values.dtype)

    @property
    def dtype_name(self) -> str:
        """Return the payload dtype as a human-readable string."""
        return str(self.dtype)

    def metadata(self) -> GroupMetadata:
        """Build metadata that matches the current group contents."""
        return GroupMetadata(
            axes=self.axes,
            field_names=self.field_names,
            dtype=self.dtype,
        )
