from typing import Optional

from pydantic import BaseModel, Field


class DeviceHealthReportBase(BaseModel):
    firmware_version: Optional[str] = Field(default=None, max_length=64)
    status_message: Optional[str] = Field(default=None, max_length=255)
    uptime_ms: Optional[int] = Field(default=None, ge=0)
    wifi_connected: Optional[bool] = None
    microphone_active: Optional[bool] = None
    ina219_online: Optional[bool] = None
    bus_voltage_v: Optional[float] = None
    shunt_voltage_mv: Optional[float] = None
    current_ma: Optional[float] = None
    power_mw: Optional[float] = None
    computed_power_mw: Optional[float] = None
    audio_queue_depth: Optional[int] = Field(default=None, ge=0)
    audio_dropped_chunks: Optional[int] = Field(default=None, ge=0)


class DeviceHealthReportCreate(DeviceHealthReportBase):
    pass


class DeviceHealthReport(DeviceHealthReportBase):
    id: int
    device_id: str
    received_at_ns: int

    class Config:
        from_attributes = True
