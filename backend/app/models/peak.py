from sqlalchemy import BigInteger, Column, String, ForeignKey, Integer, Boolean
from app.db.base_class import Base
from app.core.time_utils import epoch_ns

class Peak(Base):
    id = Column(Integer, primary_key=True, index=True)
    device_id = Column(String, ForeignKey("location.id", ondelete="CASCADE"), index=True)
    peak_time_ns = Column(BigInteger, index=True, nullable=False)
    received_at_ns = Column(BigInteger, default=epoch_ns, index=True, nullable=False)
    processed = Column(Boolean, default=False, nullable=False, index=True)
