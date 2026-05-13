from datetime import datetime
from zoneinfo import ZoneInfo
import time


WARSAW_TIMEZONE_NAME = "Europe/Warsaw"
WARSAW_TIMEZONE = ZoneInfo(WARSAW_TIMEZONE_NAME)


def epoch_ns() -> int:
    return time.time_ns()


def warsaw_offset_seconds(epoch_ns_value: int) -> int:
    dt = datetime.fromtimestamp(epoch_ns_value // 1_000_000_000, tz=WARSAW_TIMEZONE)
    offset = dt.utcoffset()
    return int(offset.total_seconds()) if offset is not None else 0
