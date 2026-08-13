import logging
from pathlib import Path

import click
import numpy as np

from .generate import (
    GenerationSizeEstimate,
    LinearFieldSpec,
    estimate_generated_group_size,
    generate_group,
)
from .io import DEFAULT_MAX_SIZE_MIB, read_group, write_group
from .model import ExplicitAxis, FieldGroup, GroupMetadata, UniformAxis

_INSPECT_BANNER = r"""
|========================================|
|           __  __    __       ___       |
|          /\ \/\ \__/\ \     /\_ \      |
|   ___    \_\ \ \ ,_\ \ \____\//\ \     |
| /' _ `\  /'_` \ \ \/\ \ '__`\ \ \ \    |
| /\ \/\ \/\ \L\ \ \ \_\ \ \L\ \ \_\ \_  |
| \ \_\ \_\ \___,_\ \__\\ \_,__/ /\____\ |
|  \/_/\/_/\/__,_ /\/__/ \/___/  \/____/ |
|                                        |
|========================================|
"""


def _configure_verbose_logging(ctx: click.Context) -> None:
    """Enable temporary debug logging for the ndtbl CLI invocation."""
    package_logger = logging.getLogger("ndtbl")
    previous_level = package_logger.level
    previous_propagate = package_logger.propagate

    handler = logging.StreamHandler()
    handler.setLevel(logging.DEBUG)
    handler.setFormatter(
        logging.Formatter("%(levelname)s %(name)s: %(message)s")
    )
    package_logger.addHandler(handler)
    package_logger.setLevel(logging.DEBUG)
    package_logger.propagate = False

    def restore_logging() -> None:
        package_logger.removeHandler(handler)
        handler.close()
        package_logger.setLevel(previous_level)
        package_logger.propagate = previous_propagate

    ctx.call_on_close(restore_logging)


class _HelpOnlyOption(click.Option):
    """Show an option in Click help without registering it for parsing."""

    def add_to_parser(self, parser, ctx) -> None:  # type: ignore[override]
        """Skip parser registration while keeping the option in help output."""
        return None


def _format_axis(axis: UniformAxis | ExplicitAxis, axis_index: int) -> str:
    """Format one axis summary line for CLI output.

    Args:
        axis: Axis object to summarize.
        axis_index: Zero-based axis index in storage order.

    Returns:
        Human-readable axis summary line.
    """

    if isinstance(axis, UniformAxis):
        return (
            f"axis[{axis_index}]: uniform "
            f"size={axis.size} min={axis.min:g} max={axis.max:g}"
        )

    coordinates = ", ".join(f"{value:g}" for value in axis.coordinates_values)
    return (
        f"axis[{axis_index}]: explicit size={axis.size} "
        f"coordinates=[{coordinates}]"
    )


def _echo_metadata(path: Path, metadata: GroupMetadata) -> None:
    """Print group metadata in the CLI summary format.

    Args:
        path: Source file path shown in the output.
        metadata: Metadata object to print.
    """

    click.echo(f"file: {path}")
    click.echo(f"format_version: {metadata.format_version}")
    click.echo(f"dimension: {metadata.dimension}")
    click.echo(f"fields: {metadata.field_count}")
    click.echo(f"points: {metadata.point_count}")
    click.echo(f"dtype: {metadata.dtype_name}")

    for axis_index, axis in enumerate(metadata.axes):
        click.echo(_format_axis(axis, axis_index))

    for field_index, field_name in enumerate(metadata.field_names):
        click.echo(f"field[{field_index}]: {field_name}")


def _echo_inspect_banner() -> None:
    """Print the inspect command ASCII art header."""

    click.echo(_INSPECT_BANNER)
    click.echo()


def _echo_samples(group: FieldGroup, samples: int) -> None:
    """Print a prefix of flattened sample values for inspection.

    Args:
        group: Field group whose values should be sampled.
        samples: Maximum number of flattened points to print.
    """

    flat_values = group.values.reshape(
        group.point_count, group.field_count, order="C"
    )
    sample_count = min(samples, group.point_count)
    click.echo(f"samples: {sample_count}")
    for sample_index in range(sample_count):
        values = ", ".join(f"{value:g}" for value in flat_values[sample_index])
        click.echo(f"sample[{sample_index}]: ({values})")


def _validate_query_indices(
    indices: tuple[int, ...], group: FieldGroup
) -> tuple[int, ...]:
    """Validate CLI query indices against a field group.

    Args:
        indices: Zero-based point indices supplied on the command line.
        group: Field group being queried.

    Returns:
        The validated indices tuple.
    """

    if len(indices) != group.dimension:
        raise click.UsageError(
            "query index count must match the table dimension: "
            f"expected {group.dimension}, got {len(indices)}"
        )

    for axis_index, (index, axis_size) in enumerate(
        zip(indices, group.axis_sizes, strict=True)
    ):
        if not 0 <= index < axis_size:
            raise click.UsageError(
                f"index for axis {axis_index} out of range: "
                f"expected 0..{axis_size - 1}, got {index}"
            )

    return indices


def _echo_query_values(group: FieldGroup, indices: tuple[int, ...]) -> None:
    """Print field values for one selected point.

    Args:
        group: Field group being queried.
        indices: Zero-based point indices in axis order.
    """

    values = group.values[indices]
    for field_name, value in zip(group.field_names, values, strict=True):
        click.echo(f"{field_name}: {value:g}")


def _parse_axes(
    axis_specs: tuple[tuple[float, float, int], ...],
) -> tuple[UniformAxis, ...]:
    """Convert CLI axis tuples into validated uniform axes.

    Args:
        axis_specs: Repeated ``(min, max, size)`` tuples from Click.

    Returns:
        Parsed axes in storage order.
    """

    if not axis_specs:
        raise click.UsageError(
            "at least one --axis MIN MAX SIZE option is required"
        )

    axes: list[UniformAxis] = []
    for min_value, max_value, size in axis_specs:
        try:
            axes.append(UniformAxis(min=min_value, max=max_value, size=size))
        except ValueError as error:
            raise click.UsageError(str(error)) from error
    return tuple(axes)


def _parse_linear_fields(
    tokens: tuple[str, ...],
    axis_count: int,
) -> tuple[Path, tuple[LinearFieldSpec, ...]]:
    """Parse positional output and repeated linear field tokens.

    Args:
        tokens: Remaining CLI tokens after Click consumes declared options.
        axis_count: Number of axes, used to validate coefficient counts.

    Returns:
        Tuple of output path and parsed field specifications.
    """

    parsed_specs: list[LinearFieldSpec] = []
    output: Path | None = None
    index = 0
    while index < len(tokens):
        option = tokens[index]
        if option not in ("--field-linear", "-f"):
            if output is not None:
                raise click.UsageError(f"unexpected extra argument: {option}")
            output = Path(option)
            index += 1
            continue
        next_index = index + 1
        expected_parts = axis_count + 2
        next_index = index + 1 + expected_parts

        parts = tokens[index + 1 : next_index]
        if len(parts) != expected_parts:
            raise click.UsageError(
                "each --field-linear option requires NAME OFFSET and"
                "one coefficient per axis"
            )

        try:
            name = parts[0]
            offset = float(parts[1])
            coefficients = tuple(float(value) for value in parts[2:])
        except ValueError as error:
            raise click.UsageError(
                "field offset and coefficients must be floats"
            ) from error

        if len(coefficients) != axis_count:
            raise click.UsageError(
                "field coefficient count must match the number of axes"
            )

        parsed_specs.append(
            LinearFieldSpec(
                name=name,
                offset=offset,
                coefficients=coefficients,
            )
        )
        index = next_index

    if output is None:
        raise click.UsageError("missing output path")
    if not parsed_specs:
        raise click.UsageError(
            "at least one --field-linear NAME OFFSET C0 [C1 ...] option"
            "is required"
        )

    return output, tuple(parsed_specs)


def _format_mib(size_bytes: int) -> str:
    """Format a byte count as a MiB string with two decimals."""
    return f"{size_bytes / (1024 * 1024):.2f}"


def _enforce_generation_size_limit(
    estimate: GenerationSizeEstimate,
    dtype_name: str,
    max_size_mib: float,
) -> None:
    """Raise a CLI error when the estimated output is too large.

    Args:
        estimate: Estimated size information for the requested output.
        dtype_name: Human-readable payload dtype name.
        max_size_mib: User-configured maximum allowed output size.
    """

    if estimate.estimated_file_mib <= max_size_mib:
        return

    raise click.ClickException(
        "generated table exceeds the configured size limit: "
        f"{estimate.point_count} points, "
        f"{estimate.field_count} fields, "
        f"dtype={dtype_name}, "
        f"estimated size={_format_mib(estimate.estimated_file_bytes)} MiB, "
        f"limit={max_size_mib:.2f} MiB. "
        "Use --max-size-mib to raise the limit explicitly."
    )


@click.group()
@click.option(
    "--verbose",
    "-v",
    is_flag=True,
    help="Show debug diagnostics on stderr.",
)
@click.pass_context
def main(ctx: click.Context, verbose: bool) -> None:
    """Read, inspect, and generate .ndtbl files."""
    if verbose:
        _configure_verbose_logging(ctx)


@main.command("inspect")
@click.argument(
    "file", type=click.Path(exists=True, dir_okay=False, path_type=Path)
)
@click.option(
    "--samples",
    "-s",
    default=5,
    show_default=True,
    type=click.IntRange(min=0),
)
@click.option(
    "--banner/--no-banner",
    default=True,
    show_default=True,
    help="Show or hide the ASCII-art header.",
)
def inspect_command(file: Path, samples: int, banner: bool) -> None:
    """Print metadata and sample payload values from an .ndtbl file.

    FILE is the input ``.ndtbl`` path.

    Use ``--samples`` to limit how many flattened sample points are shown.
    """

    try:
        group = read_group(file)
    except (OSError, ValueError) as error:
        raise click.ClickException(str(error)) from error
    if banner:
        _echo_inspect_banner()
    _echo_metadata(file, group.metadata())
    _echo_samples(group, samples)


@main.command("query")
@click.argument(
    "file", type=click.Path(exists=True, dir_okay=False, path_type=Path)
)
@click.argument("indices", nargs=-1, type=int)
@click.option(
    "--metadata",
    "-m",
    is_flag=True,
    help="Print metadata before the queried field values.",
)
def query_command(
    file: Path, indices: tuple[int, ...], metadata: bool
) -> None:
    """Print field values at one point addressed by zero-based indices.

    FILE is the input ``.ndtbl`` path.

    INDICES are zero-based point indices in axis order. Use ``--metadata`` to
    print the table summary before the queried values.
    """

    try:
        group = read_group(file)
    except (OSError, ValueError) as error:
        raise click.ClickException(str(error)) from error

    validated_indices = _validate_query_indices(indices, group)
    if metadata:
        _echo_metadata(file, group.metadata())
    _echo_query_values(group, validated_indices)


@main.command(
    "generate",
    context_settings={
        "allow_extra_args": True,
        "ignore_unknown_options": True,
    },
)
@click.argument(
    "output", required=False, type=click.Path(dir_okay=False, path_type=Path)
)
@click.option(
    "--axis",
    "-a",
    "axis_specs",
    multiple=True,
    nargs=3,
    type=(float, float, int),
    metavar="MIN MAX SIZE",
    help="Define one uniform axis. Repeat for each axis in storage order.",
)
@click.option(
    "--field-linear",
    "-f",
    cls=_HelpOnlyOption,
    expose_value=False,
    metavar="NAME OFFSET C0 [C1 ...]",
    help=(
        "Define one linear field. Provide one coefficient per axis in axis "
        "order."
    ),
)
@click.option(
    "--dtype",
    "-t",
    type=click.Choice(("float32", "float64"), case_sensitive=True),
    default="float64",
    show_default=True,
)
@click.option(
    "--max-size-mib",
    "-m",
    type=click.FloatRange(min=0.0, min_open=True),
    default=DEFAULT_MAX_SIZE_MIB,
    show_default=True,
    help="Abort generation when the estimated file size exceeds this limit.",
)
@click.pass_context
def generate_command(
    ctx: click.Context,
    output: Path | None,
    axis_specs: tuple[tuple[float, float, int], ...],
    dtype: str,
    max_size_mib: float,
) -> None:
    """Generate a simple ``.ndtbl`` file with predefined linear fields.

    OUTPUT is optional before the extra field tokens and may also be given at
    the end of the command.

    Repeat ``--axis MIN MAX SIZE`` for each axis and ``--field-linear NAME
    OFFSET C0 [C1 ...]`` for each generated field.
    """

    axes = _parse_axes(axis_specs)
    extra_tokens = tuple(ctx.args)
    if output is not None:
        extra_tokens = (str(output), *extra_tokens)
    output_path, linear_fields = _parse_linear_fields(extra_tokens, len(axes))
    estimate = estimate_generated_group_size(
        axes, linear_fields, np.dtype(dtype)
    )
    _enforce_generation_size_limit(estimate, dtype, max_size_mib)
    try:
        group = generate_group(axes, linear_fields, np.dtype(dtype))
        write_group(output_path, group, max_size_mib=max_size_mib)
    except (OSError, ValueError) as error:
        raise click.ClickException(str(error)) from error
    click.echo(f"wrote {output_path}")
