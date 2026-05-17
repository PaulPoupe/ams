import logging
import os
from typing import Any, List

from fastapi import APIRouter, Depends, HTTPException, Request
from sqlalchemy.orm import Session

from app import models, schemas
from app.core.config import settings
from app.db.session import get_db
from app.services.triangulation import process_recent_peaks

router = APIRouter()
logger = logging.getLogger(__name__)


def _require_device(db: Session, device_id: str) -> models.Location:
    device = db.query(models.Location).filter(models.Location.id == device_id).first()
    if not device:
        raise HTTPException(status_code=404, detail="Device not found")
    return device


def _audio_download_url(file_path: str | None) -> str | None:
    if not file_path:
        return None

    storage_root = os.path.abspath(settings.AUDIO_STORAGE_PATH)
    absolute_path = os.path.abspath(file_path)
    try:
        relative_path = os.path.relpath(absolute_path, storage_root)
    except ValueError:
        return None

    if relative_path.startswith(".."):
        return None

    return f"/storage/audio/{relative_path.replace(os.sep, '/')}"


def _with_audio_url(event: models.SoundEvent) -> schemas.SoundEvent:
    dto = schemas.SoundEvent.model_validate(event)
    dto.audio_download_url = _audio_download_url(event.audio_file_path)
    return dto


@router.post("/{device_id}/sound-events", response_model=schemas.SoundEvent, summary="Register detected sound event")
def create_device_sound_event(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    event_in: schemas.SoundEventCreate,
) -> Any:
    """
    Accept metadata for a locally detected impulse sound.

    This is the new event-first flow: the device sends only a detected event,
    not a continuous audio stream. A Peak is created alongside the event so the
    existing TDOA triangulation pipeline can group events from multiple devices.
    """
    _require_device(db, device_id)

    peak = models.Peak(device_id=device_id, peak_time_ns=event_in.event_time_ns)
    db.add(peak)
    db.flush()

    event = models.SoundEvent(
        device_id=device_id,
        peak_id=peak.id,
        **event_in.model_dump(),
    )
    db.add(event)
    db.commit()
    db.refresh(event)

    try:
        process_recent_peaks(db)
    except Exception:
        logger.exception("Triangulation failed after sound event %s from %s", event.id, device_id)

    logger.info(
        "Sound event %s from %s: event_time_ns=%s peak=%s rms=%s audio_uploaded=%s",
        event.id,
        device_id,
        event.event_time_ns,
        event.peak_level,
        event.rms_level,
        event.audio_uploaded,
    )
    return _with_audio_url(event)


@router.post("/{device_id}/sound-events/{event_id}/audio", response_model=schemas.SoundEvent, summary="Upload event audio")
async def upload_device_sound_event_audio(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    event_id: int,
    request: Request,
) -> Any:
    """
    Upload the 10 second audio window for a detected sound event.

    The body is raw bytes. Use `audio/wav` for WAV files or
    `application/octet-stream` for raw PCM16LE.
    """
    event = (
        db.query(models.SoundEvent)
        .filter(models.SoundEvent.id == event_id, models.SoundEvent.device_id == device_id)
        .first()
    )
    if not event:
        raise HTTPException(status_code=404, detail="Sound event not found")

    payload = await request.body()
    if not payload:
        raise HTTPException(status_code=400, detail="Audio payload is empty")

    content_type = request.headers.get("content-type", "application/octet-stream")
    extension = ".wav" if "wav" in content_type.lower() else ".pcm16"
    events_dir = os.path.join(settings.AUDIO_STORAGE_PATH, "events")
    os.makedirs(events_dir, exist_ok=True)

    file_name = f"{device_id}_{event_id}{extension}"
    file_path = os.path.join(events_dir, file_name)
    with open(file_path, "wb") as audio_file:
        audio_file.write(payload)

    event.audio_file_path = file_path
    event.audio_content_type = content_type
    event.audio_size_bytes = len(payload)
    event.audio_uploaded = True
    db.commit()
    db.refresh(event)

    logger.info("Stored audio for sound event %s from %s: %s bytes", event_id, device_id, len(payload))
    return _with_audio_url(event)


@router.get("/{device_id}/sound-events", response_model=List[schemas.SoundEvent], summary="List device sound events")
def list_device_sound_events(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    skip: int = 0,
    limit: int = 100,
) -> Any:
    _require_device(db, device_id)
    events = (
        db.query(models.SoundEvent)
        .filter(models.SoundEvent.device_id == device_id)
        .order_by(models.SoundEvent.event_time_ns.desc())
        .offset(skip)
        .limit(limit)
        .all()
    )
    return [_with_audio_url(event) for event in events]


@router.get("/{device_id}/sound-events/{event_id}", response_model=schemas.SoundEvent, summary="Get device sound event")
def read_device_sound_event(
    *,
    db: Session = Depends(get_db),
    device_id: str,
    event_id: int,
) -> Any:
    event = (
        db.query(models.SoundEvent)
        .filter(models.SoundEvent.id == event_id, models.SoundEvent.device_id == device_id)
        .first()
    )
    if not event:
        raise HTTPException(status_code=404, detail="Sound event not found")

    return _with_audio_url(event)
