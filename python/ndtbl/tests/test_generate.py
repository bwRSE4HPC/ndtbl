import numpy as np
import pytest

from ndtbl import FieldGroup, UniformAxis
from ndtbl._binary import serialized_group_size
from ndtbl.generate import (
    LinearFieldSpec,
    estimate_generated_group_size,
    generate_group,
)


def test_generate_group_builds_expected_shape_and_names() -> None:
    group = generate_group(
        axes=(UniformAxis(0.0, 1.0, 3), UniformAxis(10.0, 20.0, 2)),
        field_specs=(LinearFieldSpec("A", 1.0, (2.0, 0.0)),),
    )

    assert isinstance(group, FieldGroup)
    assert group.field_names == ("A",)
    assert group.values.shape == (3, 2, 1)
    assert group.dtype == np.dtype(np.float64)


def test_generate_group_computes_linear_field() -> None:
    group = generate_group(
        axes=(UniformAxis(0.0, 1.0, 3), UniformAxis(10.0, 20.0, 2)),
        field_specs=(LinearFieldSpec("A", 1.0, (2.0, 0.0)),),
    )

    expected = np.array(
        [
            [[1.0], [1.0]],
            [[2.0], [2.0]],
            [[3.0], [3.0]],
        ]
    )
    np.testing.assert_allclose(group.values, expected)


def test_generate_group_broadcasts_multiple_fields_across_axes() -> None:
    group = generate_group(
        axes=(UniformAxis(0.0, 1.0, 3), UniformAxis(10.0, 20.0, 2)),
        field_specs=(
            LinearFieldSpec("A", 1.0, (2.0, 0.0)),
            LinearFieldSpec("B", 5.0, (0.0, -1.0)),
        ),
        dtype=np.float32,
    )

    expected_a = np.array(
        [
            [1.0, 1.0],
            [2.0, 2.0],
            [3.0, 3.0],
        ],
        dtype=np.float32,
    )
    expected_b = np.array(
        [
            [-5.0, -15.0],
            [-5.0, -15.0],
            [-5.0, -15.0],
        ],
        dtype=np.float32,
    )

    np.testing.assert_allclose(group.values[..., 0], expected_a)
    np.testing.assert_allclose(group.values[..., 1], expected_b)
    assert group.dtype == np.dtype(np.float32)


def test_generate_group_supports_multi_axis_linear_field() -> None:
    group = generate_group(
        axes=(UniformAxis(0.0, 1.0, 3), UniformAxis(10.0, 20.0, 2)),
        field_specs=(LinearFieldSpec("T", 1.0, (2.0, 3.0)),),
    )

    expected = np.array(
        [
            [31.0, 61.0],
            [32.0, 62.0],
            [33.0, 63.0],
        ]
    )
    np.testing.assert_allclose(group.values[..., 0], expected)


def test_generate_group_rejects_mismatched_coefficient_count() -> None:
    with pytest.raises(
        ValueError,
        match="linear field coefficient count does not match axis count",
    ):
        generate_group(
            axes=(UniformAxis(0.0, 1.0, 3),),
            field_specs=(LinearFieldSpec("A", 1.0, (2.0, 3.0)),),
        )


def test_generate_group_rejects_empty_axes_or_fields() -> None:
    with pytest.raises(ValueError, match="at least one axis"):
        generate_group(
            axes=(), field_specs=(LinearFieldSpec("A", 0.0, (1.0,)),)
        )

    with pytest.raises(ValueError, match="at least one field"):
        generate_group(axes=(UniformAxis(0.0, 1.0, 2),), field_specs=())


def test_estimate_generated_group_size_counts_points_fields_and_dtype() -> (
    None
):
    estimate = estimate_generated_group_size(
        axes=(UniformAxis(0.0, 1.0, 3), UniformAxis(10.0, 20.0, 2)),
        field_specs=(
            LinearFieldSpec("A", 1.0, (2.0, 0.0)),
            LinearFieldSpec("B", 5.0, (0.0, -1.0)),
        ),
        dtype=np.float32,
    )

    assert estimate.point_count == 6
    assert estimate.field_count == 2
    assert estimate.payload_bytes == 48
    assert estimate.estimated_file_bytes > estimate.payload_bytes


def test_estimate_generated_group_size_scales_with_dtype() -> None:
    float32_estimate = estimate_generated_group_size(
        axes=(UniformAxis(0.0, 1.0, 4),),
        field_specs=(LinearFieldSpec("A", 0.0, (1.0,)),),
        dtype=np.float32,
    )
    float64_estimate = estimate_generated_group_size(
        axes=(UniformAxis(0.0, 1.0, 4),),
        field_specs=(LinearFieldSpec("A", 0.0, (1.0,)),),
        dtype=np.float64,
    )

    assert float64_estimate.payload_bytes == 2 * float32_estimate.payload_bytes
    assert (
        float64_estimate.estimated_file_bytes
        > float32_estimate.estimated_file_bytes
    )


def test_estimate_generated_group_size_matches_serialized_size() -> None:
    axes = (
        UniformAxis(0.0, 1.0, 3),
        UniformAxis(10.0, 20.0, 2),
    )
    field_specs = (
        LinearFieldSpec("A", 1.0, (2.0, 0.0)),
        LinearFieldSpec("B", 5.0, (0.0, -1.0)),
    )
    group = generate_group(axes, field_specs, dtype=np.float32)
    estimate = estimate_generated_group_size(
        axes, field_specs, dtype=np.float32
    )

    assert estimate.estimated_file_bytes == serialized_group_size(group)
