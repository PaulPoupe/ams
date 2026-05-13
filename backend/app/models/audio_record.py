from sqlalchemy import BigInteger, Column, String, ForeignKey, Integer
from sqlalchemy.orm import relationship
from app.core.time_utils import epoch_ns
from app.db.base_class import Base

class AudioRecord(Base):
    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String, ForeignKey("location.id", ondelete="CASCADE"), index=True)
    file_path = Column(String, nullable=False)
    created_at_ns = Column(BigInteger, default=epoch_ns, index=True, nullable=False)
    
    device = relationship("Location", backref="audio_records")
