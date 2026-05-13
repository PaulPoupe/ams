import logging
from typing import Any, List

from fastapi import APIRouter, BackgroundTasks, Depends, HTTPException
from sqlalchemy.orm import Session

from app.core.time_utils import WARSAW_TIMEZONE_NAME, epoch_ns, warsaw_offset_seconds
from app.db.session import SessionLocal, get_db
from app import schemas, models
from app.api.v1.endpoints import locations, audio_records, peaks

router = APIRouter()
logger = logging.getLogger(__name__)


def _require_device(db: Session, device_id: str) -> models.Location:
    device = db.query(models.Location).filter(models.Location.id == device_id).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    return device


def _record_device_time_sync_event(
    device_id: str,
    client_sent_monotonic_ns: int,
    server_received_epoch_ns: int,
    server_transmit_epoch_ns: int,
) -> None:
    db = SessionLocal()
    try:
        db_obj = models.DeviceTimeSyncEvent(
            device_id=device_id,
            client_sent_monotonic_ns=client_sent_monotonic_ns,
            server_received_epoch_ns=server_received_epoch_ns,
            server_transmit_epoch_ns=server_transmit_epoch_ns,
        )
        db.add(db_obj)
        db.commit()
    finally:
        db.close()


def _create_device_health_report(
    *,
    db: Session,
    device_id: str,
    report_in: schemas.DeviceHealthReportCreate,
) -> models.DeviceHealthReport:
    _require_device(db, device_id)

    db_obj = models.DeviceHealthReport(
        device_id=device_id,
        **report_in.model_dump()
    )
    db.add(db_obj)
    db.commit()
    db.refresh(db_obj)

    logger.info(
        "Health report from %s: wifi=%s udp=%s mic=%s ina219=%s uptime_ms=%s bus_v=%s current_ma=%s power_mw=%s queue_depth=%s dropped=%s msg=%s",
        device_id,
        db_obj.wifi_connected,
        db_obj.udp_connected,
        db_obj.microphone_active,
        db_obj.ina219_online,
        db_obj.uptime_ms,
        db_obj.bus_voltage_v,
        db_obj.current_ma,
        db_obj.power_mw,
        db_obj.audio_queue_depth,
        db_obj.audio_dropped_chunks,
        db_obj.status_message,
    )

    return db_obj

# --- Device Management (from locations.py) ---

@router.post("/", response_model=schemas.Location, summary="Register device")
def create_device(
    *,
    db: Session = Depends(get_db),
    location_in: schemas.LocationCreate,
) -> Any:
    """
    Registers a new device (microphone).
    """
    return locations.create_location(db=db, location_in=location_in)


@router.get("/{device_id}", response_model=schemas.Location, summary="Get device by ID")
def read_device(
    *,
    db: Session = Depends(get_db),
    device_id: str,
) -> Any:
    """
    Retrieve details of a specific device.
    """
    return locations.read_location(db=db, id=device_id)


@router.get("/", response_model=List[schemas.Location], summary="List all devices")
def list_devices(
    db: Session = Depends(get_db),
    skip: int = 0,
    limit: int = 100,
) -> Any:
    """
    Retrieve a list of all registered devices with their coordinates.
    """
    return locations.read_locations(db=db, skip=skip, limit=limit)

@router.put("/{device_id}", response_model=schemas.Location, summary="Update device")
def update_device(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    location_in: schemas.LocationUpdate,
) -> Any:
    """
    Update device settings (name, tag, location).
    """
    return locations.update_location(db=db, id=device_id, location_in=location_in)

@router.delete("/{device_id}", response_model=schemas.Location, summary="Delete device")
def delete_device(
    *,
    db: Session = Depends(get_db),
    device_id: str,
) -> Any:
    """
    Delete a device by its ID.
    """
    return locations.delete_location(db=db, id=device_id)

@router.get("/{device_id}/time/sync", response_model=schemas.DeviceTimeSyncResponse, summary="Synchronize device clock")
def sync_device_time(
    *,
    db: Session = Depends(get_db),
    background_tasks: BackgroundTasks,
    device_id: str,
    client_sent_monotonic_ns: int,
) -> Any:
    """
    Return server-side timestamps for NTP-style device clock synchronization.
    """
    server_received_epoch_ns = epoch_ns()
    _require_device(db, device_id)
    server_transmit_epoch_ns = epoch_ns()

    background_tasks.add_task(
        _record_device_time_sync_event,
        device_id,
        client_sent_monotonic_ns,
        server_received_epoch_ns,
        server_transmit_epoch_ns,
    )

    return {
        "device_id": device_id,
        "server_received_epoch_ns": server_received_epoch_ns,
        "server_transmit_epoch_ns": server_transmit_epoch_ns,
        "timezone_name": WARSAW_TIMEZONE_NAME,
        "timezone_offset_seconds": warsaw_offset_seconds(server_transmit_epoch_ns),
    }


@router.post("/{device_id}/health", response_model=schemas.DeviceHealthReport, summary="Report device health")
def create_device_health_report(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    report_in: schemas.DeviceHealthReportCreate,
) -> Any:
    """
    Accept a structured health report from an embedded device.
    """
    return _create_device_health_report(db=db, device_id=device_id, report_in=report_in)


@router.get("/{device_id}/health/report", response_model=schemas.DeviceHealthReport, summary="Report device health via query params")
def create_device_health_report_from_query(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    report_in: schemas.DeviceHealthReportCreate = Depends(),
) -> Any:
    """
    Accept a health report using query parameters for constrained clients such as Pico.
    """
    return _create_device_health_report(db=db, device_id=device_id, report_in=report_in)


@router.get("/{device_id}/health/latest", response_model=schemas.DeviceHealthReport, summary="Get latest device health report")
def read_latest_device_health_report(
    *,
    db: Session = Depends(get_db),
    device_id: str,
) -> Any:
    """
    Return the most recent health report received from a device.
    """
    _require_device(db, device_id)
    report = (
        db.query(models.DeviceHealthReport)
        .filter(models.DeviceHealthReport.device_id == device_id)
        .order_by(models.DeviceHealthReport.received_at_ns.desc())
        .first()
    )
    if not report:
        raise HTTPException(status_code=404, detail="No health reports found for this device")
    return report


@router.get("/{device_id}/health", response_model=List[schemas.DeviceHealthReport], summary="List device health reports")
def list_device_health_reports(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    skip: int = 0,
    limit: int = 100,
) -> Any:
    """
    Return recent health reports received from a device.
    """
    _require_device(db, device_id)
    return (
        db.query(models.DeviceHealthReport)
        .filter(models.DeviceHealthReport.device_id == device_id)
        .order_by(models.DeviceHealthReport.received_at_ns.desc())
        .offset(skip)
        .limit(limit)
        .all()
    )

# --- Device Audio Records (from audio_records.py) ---

@router.get("/{device_id}/audio_records/{record_id}", response_model=schemas.AudioRecord, summary="Get a specific record for a device")
def read_device_audio_record(
    device_id: str,
    record_id: int,
    db: Session = Depends(get_db),
) -> Any:
    """
    Retrieve details of a specific audio record for a specific device.
    """
    record = audio_records.read_record(record_id=record_id, db=db)
    if record.device_id != device_id:
        raise HTTPException(status_code=404, detail="Record not found for this device")
    return record

@router.get("/{device_id}/audio_records", response_model=List[schemas.AudioRecord], summary="List all records for a device")
def list_device_audio_records(
    device_id: str,
    db: Session = Depends(get_db),
    skip: int = 0,
    limit: int = 100,
) -> Any:
    """
    Retrieve a list of all audio records for a specific device.
    """
    return audio_records.list_device_records(device_id=device_id, db=db, skip=skip, limit=limit)

@router.delete("/{device_id}/audio_records", summary="Delete all records for a device")
def delete_device_audio_records(
    device_id: str,
    db: Session = Depends(get_db),
) -> Any:
    """
    Delete all audio records for a specific device and remove their associated files.
    """
    return audio_records.delete_device_records(device_id=device_id, db=db)

# --- Device Peaks (from peaks.py) ---

@router.get("/{device_id}/peaks", response_model=List[schemas.Peak], summary="Get peaks by device")
def read_device_peaks(
    device_id: str,
    db: Session = Depends(get_db),
    skip: int = 0,
    limit: int = 100,
) -> Any:
    """
    Retrieve all peaks registered by a specific device.
    """
    return peaks.read_peaks_by_device(device_id=device_id, db=db, skip=skip, limit=limit)
