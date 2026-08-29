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

FIXED_HEADER_SIZE = (
    len(MAGIC) + UINT8.size + UINT8.size + UINT16.size + UINT64.size * 4
)
AXIS_HEADER_SIZE = UINT8.size + UINT8.size + UINT16.size + UINT64.size
MINIMUM_AXIS_SIZE = AXIS_HEADER_SIZE + DOUBLE.size

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
    value_count: int
    payload_size: int


class _BoundedMetadataReader:
    """Read values without crossing the declared metadata boundary."""

    def __init__(self, stream: BinaryIO, remaining: int) -> None:
        self._stream = stream
        self.remaining = remaining

    def require_bytes(self, size: int, what: str) -> None:
        """Require ``size`` bytes to remain in the metadata region."""
        if size > self.remaining:
            raise NdtblFormatError(f"ndtbl {what} exceeds metadata boundary")

    def require_count(self, count: int, encoded_size: int, what: str) -> None:
        """Require space for ``count`` minimally encoded values."""
        if count > self.remaining // encoded_size:
            raise NdtblFormatError(f"ndtbl {what} exceeds metadata boundary")

    def read_exact(self, size: int, what: str) -> bytes:
        """Read bytes after checking the metadata boundary."""
        self.require_bytes(size, what)
        data = _read_exact(self._stream, size)
        self.remaining -= size
        return data

    def read_uint8(self, what: str) -> int:
        """Read one unsigned 8-bit integer."""
        return int(UINT8.unpack(self.read_exact(UINT8.size, what))[0])

    def read_uint16(self, what: str) -> int:
        """Read one unsigned 16-bit integer."""
        return int(UINT16.unpack(self.read_exact(UINT16.size, what))[0])

    def read_uint64(self, what: str) -> int:
        """Read one unsigned 64-bit integer."""
        return int(UINT64.unpack(self.read_exact(UINT64.size, what))[0])

    def read_double(self, what: str) -> float:
        """Read one IEEE-754 double-precision value."""
        return float(DOUBLE.unpack(self.read_exact(DOUBLE.size, what))[0])


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


def _read_string(reader: _BoundedMetadataReader) -> str:
    """Read a length-prefixed UTF-8 string from a stream."""
    size = reader.read_uint64("field name length")
    reader.require_bytes(size, "field name")
    data = reader.read_exact(size, "field name")
    return data.decode("utf-8")


def _write_string(stream: BinaryIO, value: str) -> None:
    """Write a length-prefixed UTF-8 string to a stream."""
    encoded = value.encode("utf-8")
    _write_uint64(stream, len(encoded))
    stream.write(encoded)


def _read_axis(reader: _BoundedMetadataReader) -> UniformAxis | ExplicitAxis:
    """Read one serialized axis definition from a stream."""
    axis_tag = reader.read_uint8("axis kind")
    _require_zero(
        reader.read_uint8("axis reserved byte"), "axis reserved byte"
    )
    _require_zero(
        reader.read_uint16("axis reserved field"), "axis reserved field"
    )
    size = reader.read_uint64("axis extent")

    try:
        if axis_tag == AXIS_KIND_UNIFORM:
            reader.require_bytes(DOUBLE.size * 2, "uniform axis coordinates")
            return UniformAxis(
                min=reader.read_double("axis minimum"),
                max=reader.read_double("axis maximum"),
                size=size,
            )

        if axis_tag == AXIS_KIND_EXPLICIT:
            reader.require_count(size, DOUBLE.size, "axis coordinates")
            coordinates = [
                reader.read_double("axis coordinate") for _ in range(size)
            ]
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


def _stream_size(stream: BinaryIO) -> int:
    """Return the stream size while preserving its current position."""
    position = stream.tell()
    try:
        stream.seek(0, 2)
        size = stream.tell()
    finally:
        stream.seek(position)
    if size < 0:
        raise OSError("failed to determine ndtbl input size")
    return size


def _read_layout_from_stream(stream: BinaryIO) -> ParsedLayout:
    """Read ndtbl metadata and validate the encoded payload offset."""
    file_size = _stream_size(stream)
    magic = _read_exact(stream, len(MAGIC))
    if magic != MAGIC:
        raise NdtblFormatError("invalid ndtbl magic header")
    if file_size < FIXED_HEADER_SIZE:
        raise NdtblFormatError("unexpected end of ndtbl file")

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

    value_count = point_count * field_count
    payload_size = value_count * dtype.itemsize
    if payload_offset < FIXED_HEADER_SIZE:
        raise NdtblFormatError("ndtbl payload offset does not match metadata")
    if payload_offset > file_size:
        raise NdtblFormatError("ndtbl payload offset exceeds file size")

    reader = _BoundedMetadataReader(stream, payload_offset - FIXED_HEADER_SIZE)
    reader.require_count(dimension, MINIMUM_AXIS_SIZE, "dimension")
    axes = tuple(_read_axis(reader) for _ in range(dimension))

    reader.require_count(field_count, UINT64.size, "field count")
    field_names = tuple(_read_string(reader) for _ in range(field_count))
    if reader.remaining != 0:
        raise NdtblFormatError("ndtbl payload offset does not match metadata")

    try:
        metadata = GroupMetadata(
            axes=axes,
            field_names=field_names,
            dtype=dtype,
            format_version=version,
        )
    except (TypeError, ValueError) as error:
        raise NdtblFormatError(str(error)) from error
    if metadata.point_count != point_count:
        raise NdtblFormatError("ndtbl point count does not match axis extents")
    if payload_offset + payload_size != file_size:
        raise NdtblFormatError(
            "ndtbl file size does not match declared payload"
        )

    return ParsedLayout(
        metadata=metadata,
        payload_offset=payload_offset,
        value_count=value_count,
        payload_size=payload_size,
    )


def read_metadata_from_stream(stream: BinaryIO) -> GroupMetadata:
    """Read ndtbl metadata from an already opened stream."""
    return _read_layout_from_stream(stream).metadata


def read_group_from_stream(stream: BinaryIO) -> FieldGroup:
    """Read an entire ndtbl file from an already opened stream."""
    layout = _read_layout_from_stream(stream)
    metadata = layout.metadata
    payload = _read_exact(stream, layout.payload_size)

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
