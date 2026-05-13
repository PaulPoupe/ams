import logging

from sqlalchemy import inspect

from app.db.session import engine
from app.models import Base


logger = logging.getLogger(__name__)

REQUIRED_COLUMNS = {
    "audio_records": {"created_at_ns"},
    "device_health_reports": {"received_at_ns"},
    "device_time_sync_events": {
        "client_sent_monotonic_ns",
        "server_received_epoch_ns",
        "server_transmit_epoch_ns",
        "created_at_ns",
    },
    "peak": {"peak_time_ns", "received_at_ns"},
    "suspicious_incidents": {"created_at_ns"},
}


def initialize_database_schema() -> None:
    inspector = inspect(engine)
    existing_tables = set(inspector.get_table_names())
    needs_reset = False

    for table_name, required_columns in REQUIRED_COLUMNS.items():
        if table_name not in existing_tables:
            continue

        existing_columns = {column["name"] for column in inspector.get_columns(table_name)}
        if not required_columns.issubset(existing_columns):
            needs_reset = True
            logger.warning("Database table %s has an old timestamp schema; recreating all tables.", table_name)
            break

    if needs_reset:
        Base.metadata.drop_all(bind=engine)

    Base.metadata.create_all(bind=engine)
