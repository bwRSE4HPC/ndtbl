import struct
from dataclasses import dataclass
from pathlib import Path
from typing import BinaryIO, cast

import numpy as np

from .model import (
    ExplicitAxis,
    FieldGroup,
    FloatArray,
    FloatDType,
    GroupMetadata,
    NdtblFormatError,
    UniformAxis,
)

MAGIC = b"NDTBL\0\0\0"
VERSION = 1

AXIS_KIND_UNIFORM = 1
AXIS_KIND_EXPLICIT = 2

SCALAR_FLOAT32 = 1
SCALAR_FLOAT64 = 2

LITTLE_ENDIAN_PREFIX = "<"
UINT8 = struct.Struct(f"{LITTLE_ENDIAN_PREFIX}B")
UINT16 = struct.Struct(f"{LITTLE_ENDIAN_PREFIX}H")
UINT64 = struct.Struct(f"{LITTLE_ENDIAN_PREFIX}Q")
DOUBLE = struct.Struct(f"{LITTLE_ENDIAN_PREFIX}d")

DTYPE_TO_TAG: dict[FloatDType, int] = {
    np.dtype(np.float32): SCALAR_FLOAT32,
    np.dtype(np.float64): SCALAR_FLOAT64,
}
TAG_TO_DTYPE: dict[int, FloatDType] = {
    SCALAR_FLOAT32: np.dtype(np.float32),
    SCALAR_FLOAT64: np.dtype(np.float64),
}


@dataclass(frozen=True, slots=True)
class ParsedLayout:
    """Parsed metadata plus payload layout information."""

    metadata: GroupMetadata
    payload_offset: int


def _read_exact(stream: BinaryIO, size: int) -> bytes:
    """Read exactly ``size`` bytes from a binary stream."""
    data = stream.read(size)
    if len(data) != size:
        raise NdtblFormatError("unexpected end of ndtbl file")
    return data


def _read_uint8(stream: BinaryIO) -> int:
    """Read one unsigned 8-bit integer from a stream."""
    return int(UINT8.unpack(_read_exact(stream, UINT8.size))[0])


def _read_uint16(stream: BinaryIO) -> int:
    """Read one unsigned 16-bit integer from a stream."""
    return int(UINT16.unpack(_read_exact(stream, UINT16.size))[0])


def _read_uint64(stream: BinaryIO) -> int:
    """Read one unsigned 64-bit integer from a stream."""
    return int(UINT64.unpack(_read_exact(stream, UINT64.size))[0])


def _read_double(stream: BinaryIO) -> float:
    """Read one IEEE-754 double-precision value from a stream."""
    return float(DOUBLE.unpack(_read_exact(stream, DOUBLE.size))[0])


def _write_uint8(stream: BinaryIO, value: int) -> None:
    """Write one unsigned 8-bit integer to a stream."""
    stream.write(UINT8.pack(value))


def _write_uint16(stream: BinaryIO, value: int) -> None:
    """Write one unsigned 16-bit integer to a stream."""
    stream.write(UINT16.pack(value))


def _write_uint64(stream: BinaryIO, value: int) -> None:
    """Write one unsigned 64-bit integer to a stream."""
    stream.write(UINT64.pack(value))


def _write_double(stream: BinaryIO, value: float) -> None:
    """Write one little-endian IEEE-754 double to a stream."""
    stream.write(DOUBLE.pack(value))


def _require_zero(value: int, what: str) -> None:
    """Reject reserved fields that are not zero."""
    if value != 0:
        raise NdtblFormatError(f"ndtbl {what} must be zero")


def _read_string(stream: BinaryIO) -> str:
    """Read a length-prefixed UTF-8 string from a stream."""
    size = _read_uint64(stream)
    data = _read_exact(stream, size)
    return data.decode("utf-8")


def _write_string(stream: BinaryIO, value: str) -> None:
    """Write a length-prefixed UTF-8 string to a stream."""
    encoded = value.encode("utf-8")
    _write_uint64(stream, len(encoded))
    stream.write(encoded)


def _read_axis(stream: BinaryIO) -> UniformAxis | ExplicitAxis:
    """Read one serialized axis definition from a stream."""
    axis_tag = _read_uint8(stream)
    _require_zero(_read_uint8(stream), "axis reserved byte")
    _require_zero(_read_uint16(stream), "axis reserved field")
    size = _read_uint64(stream)

    try:
        if axis_tag == AXIS_KIND_UNIFORM:
            return UniformAxis(
                min=_read_double(stream),
                max=_read_double(stream),
                size=size,
            )

        if axis_tag == AXIS_KIND_EXPLICIT:
            coordinates = [_read_double(stream) for _ in range(size)]
            return ExplicitAxis(coordinates)
    except ValueError as error:
        raise NdtblFormatError(str(error)) from error

    raise NdtblFormatError(f"unsupported ndtbl axis kind: {axis_tag}")


def _write_axis(stream: BinaryIO, axis: UniformAxis | ExplicitAxis) -> None:
    """Write one axis definition to a stream."""
    if isinstance(axis, UniformAxis):
        _write_uint8(stream, AXIS_KIND_UNIFORM)
        _write_uint8(stream, 0)
        _write_uint16(stream, 0)
        _write_uint64(stream, axis.size)
        _write_double(stream, axis.min)
        _write_double(stream, axis.max)
        return

    _write_uint8(stream, AXIS_KIND_EXPLICIT)
    _write_uint8(stream, 0)
    _write_uint16(stream, 0)
    _write_uint64(stream, axis.size)
    for coordinate in axis.coordinates_values:
        _write_double(stream, coordinate)


def _metadata_size(metadata: GroupMetadata) -> int:
    """Return the byte offset where the payload starts."""
    total = (
        len(MAGIC) + UINT8.size + UINT8.size + UINT16.size + UINT64.size * 4
    )

    for axis in metadata.axes:
        total += UINT8.size + UINT8.size + UINT16.size + UINT64.size
        if isinstance(axis, UniformAxis):
            total += DOUBLE.size * 2
        else:
            total += axis.size * DOUBLE.size

    for field_name in metadata.field_names:
        total += UINT64.size + len(field_name.encode("utf-8"))

    return total


def serialized_group_size(group: FieldGroup) -> int:
    """Return the exact serialized byte size for a field group."""
    metadata = group.metadata()
    payload_size = (
        metadata.point_count * metadata.field_count * metadata.dtype.itemsize
    )
    return _metadata_size(metadata) + payload_size


def _read_layout_from_stream(stream: BinaryIO) -> ParsedLayout:
    """Read ndtbl metadata and validate the encoded payload offset."""
    magic = _read_exact(stream, len(MAGIC))
    if magic != MAGIC:
        raise NdtblFormatError("invalid ndtbl magic header")

    version = _read_uint8(stream)
    if version != VERSION:
        raise NdtblFormatError(f"unsupported ndtbl version: {version}")

    scalar_tag = _read_uint8(stream)
    _require_zero(_read_uint16(stream), "header reserved field")
    payload_offset = _read_uint64(stream)

    try:
        dtype = TAG_TO_DTYPE[scalar_tag]
    except KeyError as error:
        raise NdtblFormatError(
            f"unsupported ndtbl scalar type: {scalar_tag}"
        ) from error

    dimension = _read_uint64(stream)
    field_count = _read_uint64(stream)
    point_count = _read_uint64(stream)

    axes = tuple(_read_axis(stream) for _ in range(dimension))
    field_names = tuple(_read_string(stream) for _ in range(field_count))

    metadata = GroupMetadata(
        axes=axes,
        field_names=field_names,
        dtype=dtype,
        format_version=version,
    )
    if metadata.point_count != point_count:
        raise NdtblFormatError("ndtbl point count does not match axis extents")

    actual_offset = stream.tell()
    if actual_offset != payload_offset:
        raise NdtblFormatError("ndtbl payload offset does not match metadata")

    return ParsedLayout(metadata=metadata, payload_offset=payload_offset)


def read_metadata_from_stream(stream: BinaryIO) -> GroupMetadata:
    """Read ndtbl metadata from an already opened stream."""
    return _read_layout_from_stream(stream).metadata


def read_group_from_stream(stream: BinaryIO) -> FieldGroup:
    """Read an entire ndtbl file from an already opened stream."""
    layout = _read_layout_from_stream(stream)
    metadata = layout.metadata
    value_count = metadata.point_count * metadata.field_count
    payload_size = value_count * metadata.dtype.itemsize
    payload = _read_exact(stream, payload_size)

    wire_dtype = metadata.dtype.newbyteorder("<")
    values = np.frombuffer(payload, dtype=wire_dtype).astype(
        metadata.dtype, copy=False
    )
    shaped = cast(
        FloatArray,
        values.reshape(
            (*metadata.axis_sizes, metadata.field_count), order="C"
        ),
    )
    return FieldGroup(
        axes=metadata.axes,
        field_names=metadata.field_names,
        values=shaped,
    )


def write_group_to_stream(stream: BinaryIO, group: FieldGroup) -> None:
    """Write a field group to an already opened stream."""
    metadata = group.metadata()

    try:
        scalar_tag = DTYPE_TO_TAG[metadata.dtype]
    except KeyError as error:
        raise ValueError(
            "ndtbl only supports float32 and float64 payloads"
        ) from error

    stream.write(MAGIC)
    _write_uint8(stream, VERSION)
    _write_uint8(stream, scalar_tag)
    _write_uint16(stream, 0)
    _write_uint64(stream, _metadata_size(metadata))
    _write_uint64(stream, metadata.dimension)
    _write_uint64(stream, metadata.field_count)
    _write_uint64(stream, metadata.point_count)

    for axis in metadata.axes:
        _write_axis(stream, axis)

    for field_name in metadata.field_names:
        _write_string(stream, field_name)

    wire_dtype = group.dtype.newbyteorder("<")
    wire_values = group.values.astype(wire_dtype, copy=False)
    stream.write(wire_values.reshape(-1, order="C").tobytes(order="C"))


def open_for_read(path: str | Path) -> BinaryIO:
    """Open an ndtbl file for binary reading."""
    return Path(path).open("rb")


def open_for_write(path: str | Path) -> BinaryIO:
    """Open an ndtbl file for binary writing."""
    return Path(path).open("wb")
