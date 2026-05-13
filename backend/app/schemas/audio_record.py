from typing import Optional
from pydantic import BaseModel

class AudioRecordBase(BaseModel):
    device_id: str
    file_path: str

class AudioRecordCreate(AudioRecordBase):
    pass

class AudioRecord(AudioRecordBase):
    id: int
    created_at_ns: int
    download_url: Optional[str] = None

    class Config:
        from_attributes = True
