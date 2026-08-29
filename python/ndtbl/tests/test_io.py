import logging
import struct

import numpy as np
import pytest

from ndtbl import (
    ExplicitAxis,
    FieldGroup,
    NdtblFormatError,
    UniformAxis,
    read_group,
    read_metadata,
    write_group,
)


def test_read_write_round_trip_with_uniform_axes(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "uniform.ndtbl"

    write_group(path, sample_uniform_group)
    loaded = read_group(path)

    assert loaded.axes == sample_uniform_group.axes
    assert loaded.field_names == sample_uniform_group.field_names
    assert loaded.dtype == sample_uniform_group.dtype
    np.testing.assert_array_equal(loaded.values, sample_uniform_group.values)


def test_read_write_round_trip_with_explicit_axes(
    tmp_path,
    sample_explicit_group: FieldGroup,
) -> None:
    path = tmp_path / "explicit.ndtbl"

    write_group(path, sample_explicit_group)
    loaded = read_group(path)

    assert isinstance(loaded.axes[0], ExplicitAxis)
    assert loaded.axes == sample_explicit_group.axes
    assert loaded.field_names == sample_explicit_group.field_names
    assert loaded.dtype == np.dtype(np.float32)
    np.testing.assert_allclose(loaded.values, sample_explicit_group.values)


def test_read_metadata_returns_expected_summary(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "metadata.ndtbl"
    write_group(path, sample_uniform_group)

    metadata = read_metadata(path)

    assert metadata.format_version == 1
    assert metadata.dimension == 2
    assert metadata.field_count == 2
    assert metadata.axis_sizes == (2, 3)
    assert metadata.point_count == 6
    assert metadata.field_names == ("A", "B")
    assert metadata.dtype == np.dtype(np.float64)


def test_public_io_operations_log_debug_summaries(
    caplog,
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "logged.ndtbl"

    with caplog.at_level(logging.DEBUG, logger="ndtbl.io"):
        write_group(path, sample_uniform_group)
        read_metadata(path)
        read_group(path)

    messages = [record.getMessage() for record in caplog.records]
    assert any(
        message.startswith(f"Writing ndtbl group to {path}:")
        and "dimension=2, points=6, fields=2, dtype=float64" in message
        and "size=" in message
        for message in messages
    )
    assert f"Wrote ndtbl group to {path}" in messages
    assert any(
        message.startswith(f"Read ndtbl metadata from {path}:")
        and "dimension=2, points=6, fields=2, dtype=float64" in message
        for message in messages
    )
    assert any(
        message.startswith(f"Read ndtbl group from {path}:")
        and "dimension=2, points=6, fields=2, dtype=float64" in message
        for message in messages
    )


def test_public_io_operations_are_silent_by_default(
    capsys,
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "silent.ndtbl"

    write_group(path, sample_uniform_group)
    read_metadata(path)
    read_group(path)

    captured = capsys.readouterr()
    assert captured.out == ""
    assert captured.err == ""


def test_read_group_preserves_point_major_storage_order(tmp_path) -> None:
    group = FieldGroup(
        axes=(ExplicitAxis([0.0, 1.0]), ExplicitAxis([10.0, 20.0])),
        field_names=("A", "B"),
        values=np.array(
            [
                [[0.0, 1.0], [2.0, 3.0]],
                [[4.0, 5.0], [6.0, 7.0]],
            ],
            dtype=np.float64,
        ),
    )
    path = tmp_path / "order.ndtbl"

    write_group(path, group)
    loaded = read_group(path)

    np.testing.assert_array_equal(
        loaded.values.reshape(-1, 2), group.values.reshape(-1, 2)
    )


def test_read_metadata_rejects_bad_magic(tmp_path) -> None:
    path = tmp_path / "bad-magic.ndtbl"
    path.write_bytes(b"not-ndtbl")

    with pytest.raises(NdtblFormatError, match="invalid ndtbl magic header"):
        read_metadata(path)


def test_read_metadata_rejects_unsupported_version(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "bad-version.ndtbl"
    write_group(path, sample_uniform_group)

    data = bytearray(path.read_bytes())
    data[8] = 99
    path.write_bytes(data)

    with pytest.raises(NdtblFormatError, match="unsupported ndtbl version"):
        read_metadata(path)


def test_read_metadata_rejects_unsupported_scalar_tag(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "bad-scalar.ndtbl"
    write_group(path, sample_uniform_group)

    data = bytearray(path.read_bytes())
    data[9] = 99
    path.write_bytes(data)

    with pytest.raises(
        NdtblFormatError, match="unsupported ndtbl scalar type"
    ):
        read_metadata(path)


def test_read_metadata_rejects_non_finite_axis_coordinates(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "non-finite-axis.ndtbl"
    write_group(path, sample_uniform_group)

    data = bytearray(path.read_bytes())
    struct.pack_into("<d", data, 64, float("inf"))
    path.write_bytes(data)

    with pytest.raises(
        NdtblFormatError, match="uniform axis coordinates must be finite"
    ):
        read_metadata(path)


def test_read_metadata_rejects_point_count_mismatch(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "bad-point-count.ndtbl"
    write_group(path, sample_uniform_group)

    data = bytearray(path.read_bytes())
    struct.pack_into("<Q", data, 36, 999)
    path.write_bytes(data)

    with pytest.raises(
        NdtblFormatError,
        match="ndtbl point count does not match axis extents",
    ):
        read_metadata(path)


def test_write_group_uses_expected_little_endian_layout(tmp_path) -> None:
    path = tmp_path / "exact-layout.ndtbl"
    group = FieldGroup(
        axes=(UniformAxis(2.0, 2.0, 1),),
        field_names=("A",),
        values=np.array([[1.5]], dtype=np.float32),
    )

    write_group(path, group)

    expected = b"".join(
        (
            b"NDTBL\0\0\0",
            struct.pack("<B", 1),
            struct.pack("<B", 1),
            struct.pack("<H", 0),
            struct.pack("<Q", 81),
            struct.pack("<Q", 1),
            struct.pack("<Q", 1),
            struct.pack("<Q", 1),
            struct.pack("<B", 1),
            struct.pack("<B", 0),
            struct.pack("<H", 0),
            struct.pack("<Q", 1),
            struct.pack("<d", 2.0),
            struct.pack("<d", 2.0),
            struct.pack("<Q", 1),
            b"A",
            struct.pack("<f", 1.5),
        )
    )

    assert path.read_bytes() == expected
    np.testing.assert_array_equal(read_group(path).values, group.values)


def test_write_group_enforces_default_size_limit_without_truncating(
    tmp_path,
) -> None:
    path = tmp_path / "existing.ndtbl"
    original = b"keep this file"
    path.write_bytes(original)
    group = FieldGroup(
        axes=(UniformAxis(0.0, 1.0, 2048), UniformAxis(0.0, 1.0, 2048)),
        field_names=("A", "B", "C", "D"),
        values=np.empty((2048, 2048, 4), dtype=np.float64),
    )

    with pytest.raises(
        ValueError,
        match="ndtbl file exceeds the configured size limit",
    ) as error:
        write_group(path, group)

    assert "Pass max_size_mib to raise the limit explicitly." in str(
        error.value
    )
    assert path.read_bytes() == original


def test_write_group_allows_explicitly_raised_size_limit(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "raised-limit.ndtbl"

    with pytest.raises(
        ValueError,
        match="ndtbl file exceeds the configured size limit",
    ):
        write_group(path, sample_uniform_group, max_size_mib=0.00001)
    assert not path.exists()

    write_group(path, sample_uniform_group, max_size_mib=1.0)

    loaded = read_group(path)
    np.testing.assert_array_equal(loaded.values, sample_uniform_group.values)


def test_write_group_rejects_invalid_max_size_mib(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "invalid-limit.ndtbl"

    with pytest.raises(ValueError, match="max_size_mib must be positive"):
        write_group(path, sample_uniform_group, max_size_mib=0.0)


def test_read_metadata_rejects_nonzero_reserved_header_field(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "bad-header-reserved.ndtbl"
    write_group(path, sample_uniform_group)

    data = bytearray(path.read_bytes())
    struct.pack_into("<H", data, 10, 1)
    path.write_bytes(data)

    with pytest.raises(
        NdtblFormatError, match="ndtbl header reserved field must be zero"
    ):
        read_metadata(path)


def test_read_metadata_rejects_payload_offset_mismatch(
    tmp_path,
    sample_uniform_group: FieldGroup,
) -> None:
    path = tmp_path / "bad-payload-offset.ndtbl"
    write_group(path, sample_uniform_group)

    data = bytearray(path.read_bytes())
    struct.pack_into("<Q", data, 12, 0)
    path.write_bytes(data)

    with pytest.raises(
        NdtblFormatError, match="ndtbl payload offset does not match metadata"
    ):
        read_metadata(path)
