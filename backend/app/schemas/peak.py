from pydantic import BaseModel

class PeakBase(BaseModel):
    peak_time_ns: int

class PeakCreate(PeakBase):
    pass

class PeakInDBBase(PeakBase):
    id: int
    device_id: str
    received_at_ns: int

    class Config:
        from_attributes = True

class Peak(PeakInDBBase):
    pass
