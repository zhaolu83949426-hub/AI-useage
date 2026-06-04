"""Firmware render encoder - encodes DashboardSnapshot to DashboardSnapshotV1 binary format."""

import logging
import struct
import zlib

from ..renderer.snapshot import DashboardSnapshot, ModelUsage

logger = logging.getLogger(__name__)

# Constants
PAYLOAD_SIZE_V1 = 192  # Fixed payload size for V1
MAX_MODELS = 4
MODEL_ROW_SIZE = 33
MODEL_ASCII_MAX_LEN = 24
TIME_LABEL_SIZE = 5  # HH:mm or MM-DD
GLM_LEVEL_MAX_LEN = 8

# Provider codes
PROVIDER_CODES = {
    "zhipu": 1,
    "openai": 2,
    "anthropic": 3,
    "google": 4,
}
PROVIDER_OTHER = 255


class EncodeError(Exception):
    """Raised when encoding fails."""


def encode_snapshot_v1(snapshot: DashboardSnapshot) -> bytes:
    """Encode a DashboardSnapshot to DashboardSnapshotV1 binary payload.

    Returns exactly 192 bytes.
    Raises EncodeError if validation fails.
    """
    # Validate row count
    if len(snapshot.models) > MAX_MODELS:
        raise EncodeError(f"Too many models: {len(snapshot.models)} > {MAX_MODELS}")

    buffer = bytearray(PAYLOAD_SIZE_V1)
    offset = 0

    # Header (offset 0-1)
    buffer[offset] = 1  # schema_version
    offset += 1
    buffer[offset] = len(snapshot.models)  # row_count
    offset += 1

    # GLM/GPT percentages (offset 2-5)
    _validate_percent(snapshot.glm_plan.five_hour_percent)
    _validate_percent(snapshot.glm_plan.week_percent)
    _validate_percent(snapshot.gpt_plan.five_hour_percent)
    _validate_percent(snapshot.gpt_plan.week_percent)

    buffer[offset] = snapshot.glm_plan.five_hour_percent
    offset += 1
    buffer[offset] = snapshot.glm_plan.week_percent
    offset += 1
    buffer[offset] = snapshot.gpt_plan.five_hour_percent
    offset += 1
    buffer[offset] = snapshot.gpt_plan.week_percent
    offset += 1

    # GLM level length (offset 6)
    glm_level = snapshot.glm_plan.plan_level or ""
    if not isinstance(glm_level, str):
        glm_level = str(glm_level)
    glm_level = glm_level.upper()[:GLM_LEVEL_MAX_LEN]
    buffer[offset] = len(glm_level.encode("ascii"))
    offset += 1

    # Reserved (offset 7)
    offset += 1

    # Token counts (offset 8-23)
    struct.pack_into("<I", buffer, offset, snapshot.total_tokens)
    offset += 4
    struct.pack_into("<I", buffer, offset, snapshot.input_tokens)
    offset += 4
    struct.pack_into("<I", buffer, offset, snapshot.output_tokens)
    offset += 4
    struct.pack_into("<I", buffer, offset, snapshot.cache_tokens)
    offset += 4

    # Time labels (offset 24-48)
    _write_time_label(buffer, offset, snapshot.last_refresh_label)
    offset += TIME_LABEL_SIZE
    _write_time_label(buffer, offset, snapshot.glm_plan.five_hour_label)
    offset += TIME_LABEL_SIZE
    _write_time_label(buffer, offset, snapshot.glm_plan.week_label)
    offset += TIME_LABEL_SIZE
    _write_time_label(buffer, offset, snapshot.gpt_plan.five_hour_label)
    offset += TIME_LABEL_SIZE
    _write_time_label(buffer, offset, snapshot.gpt_plan.week_label)
    offset += TIME_LABEL_SIZE

    # GLM level string (offset 49-56)
    glm_level_bytes = glm_level.encode("ascii", errors="replace")
    buffer[offset:offset + len(glm_level_bytes)] = glm_level_bytes
    offset += GLM_LEVEL_MAX_LEN

    # Model rows (offset 57-188)
    for model_usage in snapshot.models:
        _encode_model_row(buffer, offset, model_usage)
        offset += MODEL_ROW_SIZE

    # Padding models if needed
    while offset < 189:
        _encode_model_row(buffer, offset, None)
        offset += MODEL_ROW_SIZE

    # Time sync data (bytes 189-191)
    buffer[offset] = snapshot.sync_hour
    offset += 1
    buffer[offset] = snapshot.sync_minute
    offset += 1
    buffer[offset] = snapshot.sync_second
    offset += 1

    return bytes(buffer)


def _write_time_label(buffer: bytearray, offset: int, label: str) -> None:
    """Write a 5-byte time label (HH:mm or MM-DD), padded with zeros."""
    label_bytes = label[:TIME_LABEL_SIZE].encode("ascii", errors="replace")
    buffer[offset:offset + len(label_bytes)] = label_bytes
    # Remaining bytes stay zero


def _encode_model_row(buffer: bytearray, offset: int, model: ModelUsage | None) -> None:
    """Encode one model row (33 bytes)."""
    if model is None:
        # Empty row - all zeros
        return

    # Model name (offset 0-23)
    model_ascii = _sanitize_model_name(model.model)
    model_bytes = model_ascii[:MODEL_ASCII_MAX_LEN].encode("ascii", errors="replace")
    buffer[offset:offset + len(model_bytes)] = model_bytes
    offset += MODEL_ASCII_MAX_LEN

    # Provider code (offset 24)
    provider_code = PROVIDER_CODES.get(model.provider.lower(), PROVIDER_OTHER)
    buffer[offset] = provider_code
    offset += 1

    # Calls (offset 25-26)
    struct.pack_into("<H", buffer, offset, min(model.calls, 0xFFFF))
    offset += 2

    # Total tokens (offset 27-30)
    struct.pack_into("<I", buffer, offset, model.total_tokens)
    offset += 4

    # Share in basis points (offset 31-32)
    # Convert percent (0-100) to basis points (0-10000)
    share_bp = int(model.share_percent * 100)
    share_bp = max(0, min(share_bp, 10000))
    struct.pack_into("<H", buffer, offset, share_bp)


def _sanitize_model_name(name: str) -> str:
    """Sanitize model name to ASCII only, replacing non-ASCII with _."""
    if not isinstance(name, str):
        name = str(name)

    # Replace common non-ASCII model names with ASCII aliases
    aliases = {
        "文心一言": "ernie",
        "通义千问": "qwen",
        "deepseek": "deepseek",
    }
    for chinese, alias in aliases.items():
        if chinese in name:
            name = name.replace(chinese, alias)

    # Replace any remaining non-ASCII with underscore
    result = []
    for ch in name:
        if ord(ch) < 128:
            result.append(ch)
        else:
            result.append("_")
    return "".join(result)


def _validate_percent(value: int) -> None:
    """Validate percentage is in valid range."""
    if not (0 <= value <= 100):
        raise EncodeError(f"Invalid percentage: {value}, must be 0-100")


def calculate_crc32(data: bytes) -> int:
    """Calculate CRC32 checksum (little-endian)."""
    return zlib.crc32(data) & 0xFFFFFFFF


def encode_with_crc(snapshot: DashboardSnapshot) -> tuple[bytes, int]:
    """Encode snapshot and return (payload, crc32)."""
    payload = encode_snapshot_v1(snapshot)
    crc = calculate_crc32(payload)
    return payload, crc


def format_snapshot_for_debug(snapshot: DashboardSnapshot) -> str:
    """Format snapshot for debug logging."""
    lines = [
        f"DashboardSnapshot at {snapshot.generated_at}:",
        f"  last_refresh: {snapshot.last_refresh_label}",
        f"  tokens: total={snapshot.total_tokens} "
        f"in={snapshot.input_tokens} out={snapshot.output_tokens} cache={snapshot.cache_tokens}",
        f"  glm: level={snapshot.glm_plan.plan_level} "
        f"5h={snapshot.glm_plan.five_hour_percent}% w={snapshot.glm_plan.week_percent}%",
        f"  gpt: 5h={snapshot.gpt_plan.five_hour_percent}% w={snapshot.gpt_plan.week_percent}%",
        f"  models: {len(snapshot.models)}",
    ]
    for i, m in enumerate(snapshot.models):
        lines.append(f"    [{i}] {m.model}/{m.provider}: "
                    f"{m.calls} calls, {m.total_tokens} tokens, {m.share_percent}%")
    return "\n".join(lines)
