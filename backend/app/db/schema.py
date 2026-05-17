from app.db.session import engine
from app.models import Base


def create_database_schema() -> None:
    Base.metadata.create_all(bind=engine)
