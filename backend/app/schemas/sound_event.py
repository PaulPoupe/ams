from typing import Optional

from pydantic import BaseModel, Field


class SoundEventBase(BaseModel):
    event_time_ns: int = Field(..., description="Exact UTC time of the detected acoustic peak, in nanoseconds.")
    duration_ms: int = 10000
    pre_event_ms: int = 5000
    post_event_ms: int = 5000
    sample_rate_hz: int = 16000
    channels: int = 1
    sample_format: str = "pcm16le"
    peak_level: Optional[float] = None
    rms_level: Optional[float] = None
    noise_floor: Optional[float] = None
    detector_version: str = "embedded-basic-v1"
    detection_label: str = "impulse"


class SoundEventCreate(SoundEventBase):
    pass


class SoundEvent(SoundEventBase):
    id: int
    device_id: str
    peak_id: Optional[int] = None
    received_at_ns: int
    audio_file_path: Optional[str] = None
    audio_content_type: Optional[str] = None
    audio_size_bytes: Optional[int] = None
    audio_uploaded: bool
    processed: bool
    audio_download_url: Optional[str] = None

    class Config:
        from_attributes = True
