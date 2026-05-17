from typing import Optional
from pydantic import BaseModel
from pydantic import Field
from app.schemas.device_health import DeviceHealthReport

class LocationBase(BaseModel):
    name: str
    tag: Optional[str] = None

class LocationCreate(LocationBase):
    id: Optional[str] = Field(default=None, min_length=1, max_length=64, pattern=r"^[A-Za-z0-9_-]+$")
    lat: float
    lon: float

class LocationUpdate(BaseModel):
    name: Optional[str] = None
    tag: Optional[str] = None
    lat: Optional[float] = None
    lon: Optional[float] = None

class LocationInDBBase(LocationBase):
    id: str

    class Config:
        from_attributes = True


class DeviceTimeSyncEvent(BaseModel):
    id: int
    device_id: str
    client_sent_monotonic_ns: int
    server_received_epoch_ns: int
    server_transmit_epoch_ns: int
    created_at_ns: int

    class Config:
        from_attributes = True


class Location(LocationInDBBase):
    lat: Optional[float] = None
    lon: Optional[float] = None
    latest_health: Optional[DeviceHealthReport] = None
    latest_time_sync: Optional[DeviceTimeSyncEvent] = None

class DeviceTimeSyncResponse(BaseModel):
    device_id: str
    server_received_epoch_ns: int
    server_transmit_epoch_ns: int
    timezone_name: str
    timezone_offset_seconds: int
