"""Read and write ndtbl files."""

import logging
from pathlib import Path

from ._binary import (
    open_for_read,
    open_for_write,
    read_group_from_stream,
    read_metadata_from_stream,
    serialized_group_size,
    write_group_to_stream,
)
from .model import FieldGroup, GroupMetadata

logger = logging.getLogger(__name__)

DEFAULT_MAX_SIZE_MIB = 128.0
_BYTES_PER_MIB = 1024 * 1024


def _format_mib(size_bytes: int) -> str:
    """Format a byte count as a MiB string with two decimals."""
    return f"{size_bytes / _BYTES_PER_MIB:.2f}"


def _normalize_max_size_mib(max_size_mib: float) -> float:
    """Return a validated file size limit in mebibytes."""
    limit = float(max_size_mib)
    if limit <= 0.0:
        raise ValueError("max_size_mib must be positive")
    return limit


def _enforce_file_size_limit(
    estimated_file_bytes: int,
    group: FieldGroup,
    max_size_mib: float,
    *,
    subject: str = "ndtbl file",
    override_hint: str = "Pass max_size_mib to raise the limit explicitly.",
) -> None:
    """Raise ``ValueError`` when a planned ndtbl write is too large."""
    limit = _normalize_max_size_mib(max_size_mib)
    if estimated_file_bytes <= limit * _BYTES_PER_MIB:
        return

    raise ValueError(
        f"{subject} exceeds the configured size limit: "
        f"{group.point_count} points, "
        f"{group.field_count} fields, "
        f"dtype={group.dtype_name}, "
        f"estimated size={_format_mib(estimated_file_bytes)} MiB, "
        f"limit={limit:.2f} MiB. "
        f"{override_hint}"
    )


def read_metadata(path: str | Path) -> GroupMetadata:
    """Read only metadata from an ndtbl file.

    Args:
        path: Path to the input ``.ndtbl`` file.

    Returns:
        The parsed group metadata.

    """
    path = Path(path)
    logger.debug("Reading ndtbl metadata from %s", path)
    with open_for_read(path) as stream:
        group_metadata = read_metadata_from_stream(stream)

    logger.debug(
        "Read ndtbl metadata from %s: dimension=%d, points=%d, "
        "fields=%d, dtype=%s",
        path,
        group_metadata.dimension,
        group_metadata.point_count,
        group_metadata.field_count,
        group_metadata.dtype_name,
    )
    return group_metadata


def read_group(path: str | Path) -> FieldGroup:
    """Read a complete ndtbl file into memory.

    Args:
        path: Path to the input ``.ndtbl`` file.

    Returns:
        The parsed field group including payload values.

    """
    path = Path(path)
    logger.debug("Reading ndtbl group from %s", path)
    with open_for_read(path) as stream:
        group = read_group_from_stream(stream)

    logger.debug(
        "Read ndtbl group from %s: dimension=%d, points=%d, "
        "fields=%d, dtype=%s",
        path,
        group.dimension,
        group.point_count,
        group.field_count,
        group.dtype_name,
    )
    return group


def write_group(
    path: str | Path,
    group: FieldGroup,
    *,
    max_size_mib: float = DEFAULT_MAX_SIZE_MIB,
) -> None:
    """Write a field group to an ndtbl file.

    Args:
        path: Destination path for the output ``.ndtbl`` file.
        group: In-memory field group to serialize.
        max_size_mib: Maximum serialized file size allowed before writing.

    """
    path = Path(path)
    serialized_bytes = serialized_group_size(group)
    _enforce_file_size_limit(serialized_bytes, group, max_size_mib)
    logger.debug(
        "Writing ndtbl group to %s: dimension=%d, points=%d, "
        "fields=%d, dtype=%s, size=%d bytes",
        path,
        group.dimension,
        group.point_count,
        group.field_count,
        group.dtype_name,
        serialized_bytes,
    )
    with open_for_write(path) as stream:
        write_group_to_stream(stream, group)
    logger.debug("Wrote ndtbl group to %s", path)
