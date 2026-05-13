from sqlalchemy import BigInteger, Column, Integer, String
from geoalchemy2 import Geometry
from app.db.base_class import Base
from app.core.time_utils import epoch_ns

class SuspiciousIncident(Base):
    __tablename__ = "suspicious_incidents"
    id = Column(Integer, primary_key=True, index=True)
    description = Column(String, nullable=True)
    geom = Column(Geometry(geometry_type='POINT', srid=4326))
    created_at_ns = Column(BigInteger, default=epoch_ns, index=True, nullable=False)
