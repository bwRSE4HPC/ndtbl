import numpy as np

from ndtbl import FieldGroup, UniformAxis, read_group, write_group
from ndtbl.cli import _INSPECT_BANNER, main


def test_inspect_prints_key_metadata_and_default_samples(
    runner,
    tmp_path,
    sample_uniform_group,
) -> None:
    path = tmp_path / "inspect.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["inspect", str(path)])

    assert result.exit_code == 0
    assert f"{_INSPECT_BANNER}\n\nfile: {path}" in result.output
    assert f"file: {path}" in result.output
    assert "format_version: 1" in result.output
    assert "dimension: 2" in result.output
    assert "fields: 2" in result.output
    assert "points: 6" in result.output
    assert "dtype: float64" in result.output
    assert "axis[0]: uniform size=2 min=0 max=1" in result.output
    assert "field[0]: A" in result.output
    assert "samples: 5" in result.output
    assert "sample[0]: (0, 1)" in result.output
    assert "sample[4]: (8, 9)" in result.output
    assert "sample[5]" not in result.output


def test_inspect_no_banner_suppresses_ascii_header(
    runner,
    tmp_path,
    sample_uniform_group,
) -> None:
    path = tmp_path / "inspect-no-banner.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["inspect", str(path), "--no-banner"])

    assert result.exit_code == 0
    assert _INSPECT_BANNER not in result.output
    assert f"file: {path}" in result.output
    assert "samples: 5" in result.output


def test_verbose_inspect_logs_diagnostics_to_stderr(
    runner,
    tmp_path,
    sample_uniform_group,
) -> None:
    path = tmp_path / "inspect-verbose.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(
        main, ["--verbose", "inspect", str(path), "--no-banner"]
    )

    assert result.exit_code == 0
    assert result.stdout.startswith(f"file: {path}\n")
    assert "DEBUG ndtbl.io: Reading ndtbl group from" in result.stderr
    assert f"Read ndtbl group from {path}:" in result.stderr
    assert "DEBUG ndtbl.io:" not in result.stdout


def test_verbose_logging_does_not_leak_into_later_invocations(
    runner,
    tmp_path,
    sample_uniform_group,
) -> None:
    path = tmp_path / "inspect-after-verbose.ndtbl"
    write_group(path, sample_uniform_group)

    verbose_result = runner.invoke(
        main, ["--verbose", "inspect", str(path), "--no-banner"]
    )
    result = runner.invoke(main, ["inspect", str(path), "--no-banner"])

    assert verbose_result.exit_code == 0
    assert result.exit_code == 0
    assert result.stderr == ""
    assert "DEBUG ndtbl.io:" not in result.output


def test_verbose_failure_reports_click_error_once(runner, tmp_path) -> None:
    path = tmp_path / "invalid-verbose.ndtbl"
    path.write_bytes(b"not an ndtbl file")

    result = runner.invoke(main, ["--verbose", "inspect", str(path)])

    assert result.exit_code != 0
    assert result.stderr.count("Reading ndtbl group from") == 1
    assert result.stderr.count("Error: invalid ndtbl magic header") == 1
    assert "ERROR ndtbl.io:" not in result.stderr


def test_inspect_short_samples_option_limits_output(
    runner,
    tmp_path,
    sample_uniform_group,
) -> None:
    path = tmp_path / "inspect-short.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["inspect", str(path), "-s", "1"])

    assert result.exit_code == 0
    assert "samples: 1" in result.output
    assert "sample[0]: (0, 1)" in result.output
    assert "sample[1]" not in result.output


def test_inspect_invalid_file_reports_error_without_banner(
    runner, tmp_path
) -> None:
    path = tmp_path / "invalid.ndtbl"
    path.write_bytes(b"not an ndtbl file")

    result = runner.invoke(main, ["inspect", str(path)])

    assert result.exit_code != 0
    assert _INSPECT_BANNER not in result.output
    assert "Error: invalid ndtbl magic header" in result.stderr


def test_query_prints_values_for_requested_point(
    runner, tmp_path, sample_uniform_group
) -> None:
    path = tmp_path / "query.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["query", str(path), "1", "2"])

    assert result.exit_code == 0
    assert result.output == "A: 10\nB: 11\n"


def test_query_prints_values_for_requested_point_in_3d_table(
    runner, tmp_path
) -> None:
    path = tmp_path / "query-3d.ndtbl"
    group = FieldGroup(
        axes=(
            UniformAxis(0.0, 1.0, 2),
            UniformAxis(0.0, 2.0, 3),
            UniformAxis(0.0, 1.0, 2),
        ),
        field_names=("A", "B"),
        values=np.arange(24, dtype=np.float64).reshape(2, 3, 2, 2),
    )
    write_group(path, group)

    result = runner.invoke(main, ["query", str(path), "1", "2", "1"])

    assert result.exit_code == 0
    assert result.output == "A: 22\nB: 23\n"


def test_query_metadata_prints_metadata_before_values(
    runner, tmp_path, sample_uniform_group
) -> None:
    path = tmp_path / "query-metadata.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["query", "-m", str(path), "0", "1"])

    assert result.exit_code == 0
    assert f"file: {path}" in result.output
    assert "dimension: 2" in result.output
    assert "field[0]: A" in result.output
    assert result.output.rstrip().endswith("A: 2\nB: 3")


def test_query_requires_matching_dimension(
    runner, tmp_path, sample_uniform_group
) -> None:
    path = tmp_path / "query-dimension.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["query", str(path), "1"])

    assert result.exit_code != 0
    assert "expected 2, got 1" in result.output


def test_query_rejects_too_many_indices(
    runner, tmp_path, sample_uniform_group
) -> None:
    path = tmp_path / "query-too-many.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["query", str(path), "0", "1", "2"])

    assert result.exit_code != 0
    assert "expected 2, got 3" in result.output


def test_query_rejects_out_of_range_indices(
    runner, tmp_path, sample_uniform_group
) -> None:
    path = tmp_path / "query-range.ndtbl"
    write_group(path, sample_uniform_group)

    result = runner.invoke(main, ["query", str(path), "2", "1"])

    assert result.exit_code != 0
    assert "index for axis 0 out of range" in result.output


def test_query_invalid_file_reports_click_error(runner, tmp_path) -> None:
    path = tmp_path / "invalid-query.ndtbl"
    path.write_bytes(b"not an ndtbl file")

    result = runner.invoke(main, ["query", str(path), "0"])

    assert result.exit_code != 0
    assert "Error: invalid ndtbl magic header" in result.stderr


def test_generate_writes_expected_file_with_long_options(
    runner, tmp_path
) -> None:
    path = tmp_path / "generated-long.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            str(path),
            "--axis",
            "0",
            "1",
            "3",
            "--axis",
            "10",
            "20",
            "2",
            "--field-linear",
            "A",
            "1.0",
            "2.0",
            "0.0",
            "--field-linear",
            "B",
            "5.0",
            "0.0",
            "-1.0",
            "--dtype",
            "float32",
        ],
    )

    assert result.exit_code == 0
    assert f"wrote {path}" in result.output

    group = read_group(path)
    assert group.field_names == ("A", "B")
    assert group.dtype == np.dtype(np.float32)
    np.testing.assert_allclose(
        group.values[..., 0],
        np.array([[1.0, 1.0], [2.0, 2.0], [3.0, 3.0]], dtype=np.float32),
    )


def test_generate_accepts_output_path_at_end(runner, tmp_path) -> None:
    path = tmp_path / "generated-end.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            "-a",
            "0",
            "1",
            "2",
            "-a",
            "10",
            "20",
            "2",
            "-f",
            "A",
            "1.0",
            "2.0",
            "0.0",
            str(path),
        ],
    )

    assert result.exit_code == 0
    assert f"wrote {path}" in result.output

    group = read_group(path)
    np.testing.assert_allclose(
        group.values[..., 0],
        np.array([[1.0, 1.0], [3.0, 3.0]]),
    )


def test_generate_writes_expected_file_with_short_options(
    runner, tmp_path
) -> None:
    path = tmp_path / "generated-short.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            str(path),
            "-a",
            "0",
            "1",
            "2",
            "-f",
            "A",
            "2.0",
            "3.0",
            "-f",
            "B",
            "1.0",
            "0.0",
            "-t",
            "float64",
        ],
    )

    assert result.exit_code == 0
    assert f"wrote {path}" in result.output

    group = read_group(path)
    np.testing.assert_allclose(group.values[..., 0], np.array([2.0, 5.0]))
    np.testing.assert_allclose(group.values[..., 1], np.array([1.0, 1.0]))


def test_generate_requires_axis_option(runner, tmp_path) -> None:
    path = tmp_path / "missing-axis.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            str(path),
            "--field-linear",
            "A",
            "1.0",
            "1.0",
            "2.0",
        ],
    )

    assert result.exit_code != 0
    assert (
        "at least one --axis MIN MAX SIZE option is required" in result.output
    )


def test_generate_requires_output_path(runner) -> None:
    result = runner.invoke(
        main,
        [
            "generate",
            "--axis",
            "0",
            "1",
            "2",
            "--field-linear",
            "A",
            "1.0",
            "2.0",
        ],
    )

    assert result.exit_code != 0
    assert "missing output path" in result.output


def test_generate_reports_mismatched_coefficient_count(
    runner, tmp_path
) -> None:
    path = tmp_path / "bad-coeff-count.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            str(path),
            "--axis",
            "0",
            "1",
            "2",
            "--field-linear",
            "A",
            "1.0",
            "1.0",
            "2.0",
        ],
    )

    assert result.exit_code != 0
    assert "Error:" in result.output


def test_generate_reports_non_numeric_coefficients(runner, tmp_path) -> None:
    path = tmp_path / "bad-coeff-format.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            str(path),
            "--axis",
            "0",
            "1",
            "2",
            "--field-linear",
            "A",
            "1.0",
            "1.0",
            "not-a-number",
        ],
    )

    assert result.exit_code != 0
    assert "Error:" in result.output


def test_generate_enforces_default_size_limit(runner, tmp_path) -> None:
    path = tmp_path / "too-large.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            "--axis",
            "0",
            "1",
            "2048",
            "--axis",
            "0",
            "1",
            "2048",
            "--field-linear",
            "A",
            "0.0",
            "1.0",
            "1.0",
            "--field-linear",
            "B",
            "0.0",
            "1.0",
            "0.0",
            "--field-linear",
            "C",
            "0.0",
            "0.0",
            "1.0",
            "--field-linear",
            "D",
            "1.0",
            "1.0",
            "-1.0",
            "--dtype",
            "float64",
            str(path),
        ],
    )

    assert result.exit_code != 0
    assert "generated table exceeds the configured size limit" in result.output
    assert "Use --max-size-mib to raise the limit explicitly." in result.output


def test_generate_allows_explicitly_raised_size_limit(
    runner, tmp_path
) -> None:
    path = tmp_path / "allowed-large.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            "--axis",
            "0",
            "1",
            "2048",
            "--axis",
            "0",
            "1",
            "2048",
            "--field-linear",
            "A",
            "0.0",
            "1.0",
            "1.0",
            "--field-linear",
            "B",
            "0.0",
            "1.0",
            "0.0",
            "--field-linear",
            "C",
            "0.0",
            "0.0",
            "1.0",
            "--field-linear",
            "D",
            "1.0",
            "1.0",
            "-1.0",
            "--dtype",
            "float64",
            "--max-size-mib",
            "256",
            str(path),
        ],
    )

    assert result.exit_code == 0
    assert f"wrote {path}" in result.output


def test_generate_rejects_invalid_max_size_mib(runner, tmp_path) -> None:
    path = tmp_path / "invalid-limit.ndtbl"

    result = runner.invoke(
        main,
        [
            "generate",
            "--axis",
            "0",
            "1",
            "2",
            "--field-linear",
            "A",
            "0.0",
            "1.0",
            "--max-size-mib",
            "0",
            str(path),
        ],
    )

    assert result.exit_code != 0
    assert "Invalid value for '--max-size-mib' / '-m'" in result.output
