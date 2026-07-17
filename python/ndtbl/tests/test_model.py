import numpy as np
import pytest
from numpy.typing import ArrayLike

from ndtbl import ExplicitAxis, FieldGroup, GroupMetadata, UniformAxis


def test_uniform_axis_normalizes_single_point_axis() -> None:
    axis = UniformAxis(min=2.5, max=10.0, size=1)

    assert axis.min == 2.5
    assert axis.max == 2.5
    np.testing.assert_array_equal(axis.coordinates(), np.array([2.5]))


@pytest.mark.parametrize(
    ("min_value", "max_value", "size"),
    [
        (0.0, 1.0, 0),
        (1.0, 1.0, 2),
        (2.0, 1.0, 2),
    ],
)
def test_uniform_axis_rejects_invalid_ranges(
    min_value: float,
    max_value: float,
    size: int,
) -> None:
    with pytest.raises(ValueError):
        UniformAxis(min=min_value, max=max_value, size=size)


def test_explicit_axis_accepts_strictly_increasing_coordinates() -> None:
    axis = ExplicitAxis([0.0, 0.5, 1.5])

    assert axis.size == 3
    assert axis.min == 0.0
    assert axis.max == 1.5
    np.testing.assert_allclose(axis.coordinates(), np.array([0.0, 0.5, 1.5]))


@pytest.mark.parametrize(
    "coordinates",
    [
        [],
        [[0.0, 1.0]],
        [0.0, 0.0, 1.0],
        [0.0, 1.0, 0.5],
    ],
)
def test_explicit_axis_rejects_invalid_coordinates(
    coordinates: ArrayLike,
) -> None:
    with pytest.raises(ValueError):
        ExplicitAxis(coordinates)


def test_field_group_normalizes_numeric_inputs_to_float64() -> None:
    group = FieldGroup(
        axes=[UniformAxis(0.0, 1.0, 2)],
        field_names=["A"],
        values=[[-1], [2]],
    )

    assert isinstance(group.axes, tuple)
    assert group.field_names == ("A",)
    assert group.dtype == np.dtype(np.float64)
    np.testing.assert_array_equal(group.values, np.array([[-1.0], [2.0]]))


def test_field_group_rejects_invalid_shape() -> None:
    with pytest.raises(ValueError):
        FieldGroup(
            axes=(UniformAxis(0.0, 1.0, 2), UniformAxis(0.0, 1.0, 3)),
            field_names=("A", "B"),
            values=np.zeros((2, 3), dtype=np.float64),
        )


def test_field_group_rejects_empty_or_blank_field_names() -> None:
    with pytest.raises(ValueError):
        FieldGroup(
            axes=(UniformAxis(0.0, 1.0, 2),),
            field_names=(),
            values=np.zeros((2, 0), dtype=np.float64),
        )

    with pytest.raises(ValueError):
        FieldGroup(
            axes=(UniformAxis(0.0, 1.0, 2),),
            field_names=("A", ""),
            values=np.zeros((2, 2), dtype=np.float64),
        )


def test_group_metadata_reports_derived_properties() -> None:
    metadata = GroupMetadata(
        axes=[UniformAxis(0.0, 1.0, 2), ExplicitAxis([5.0, 6.0, 7.0])],
        field_names=["A", "B"],
        dtype=np.float32,
    )

    assert isinstance(metadata.axes, tuple)
    assert metadata.field_names == ("A", "B")
    assert metadata.dimension == 2
    assert metadata.field_count == 2
    assert metadata.axis_sizes == (2, 3)
    assert metadata.point_count == 6
    assert metadata.dtype_name == "float32"
