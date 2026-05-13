from pydantic import BaseModel
from typing import Optional

class SuspiciousIncidentBase(BaseModel):
    description: Optional[str] = None

class SuspiciousIncidentCreate(SuspiciousIncidentBase):
    lat: Optional[float] = None
    lon: Optional[float] = None

class SuspiciousIncidentInDBBase(SuspiciousIncidentBase):
    id: int
    created_at_ns: int

    class Config:
        from_attributes = True

class SuspiciousIncident(SuspiciousIncidentInDBBase):
    lat: Optional[float] = None
    lon: Optional[float] = None
