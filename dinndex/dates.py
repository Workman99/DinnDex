"""Choose dates without assigning a timezone to naive EXIF wall-clock times."""
from datetime import datetime, timezone
import re
from typing import Any


def reconcile(record: dict[str, Any], sidecars: list[dict[str, Any]],
              use_mtime: bool = False) -> dict[str, Any]:
    candidates: list[dict[str, str]] = []
    warnings: list[str] = []
    embedded = record.get('embedded_dates', {})

    def add(value: str, source: str, confidence: str) -> None:
        candidates.append(dict(timestamp=value, source=source, confidence=confidence))

    def exif(key: str, offset_key: str, source: str) -> None:
        raw = embedded.get(key)
        if not raw:
            return
        try:
            dt = datetime.strptime(raw.strip(), '%Y:%m:%d %H:%M:%S')
            offset = embedded.get(offset_key)
            if offset:
                if not re.fullmatch(r'[+-]\d{2}:\d{2}', offset):
                    raise ValueError('invalid EXIF offset')
                dt = datetime.fromisoformat(dt.isoformat() + offset)
            add(dt.isoformat(), source, 'high' if source == 'exif_original' else 'medium')
        except (ValueError, TypeError) as exc:
            warnings.append(f'{key}: {exc}')

    exif('Exif.Photo.DateTimeOriginal', 'Exif.Photo.OffsetTimeOriginal', 'exif_original')
    for sidecar in sidecars:
        try:
            raw = sidecar['metadata'].get('photoTakenTime', {}).get('timestamp')
            if raw is None:
                if 'photoTakenTime' in sidecar['metadata']:
                    warnings.append(f"{sidecar['path']}: photoTakenTime.timestamp is missing")
                continue
            dt = datetime.fromtimestamp(int(raw), timezone.utc)
            add(dt.isoformat(), 'takeout', 'high')
        except (ValueError, TypeError, AttributeError, OverflowError, OSError) as exc:
            warnings.append(f"{sidecar['path']}: invalid photoTakenTime ({exc})")
    exif('Exif.Photo.DateTimeDigitized', 'Exif.Photo.OffsetTimeDigitized', 'exif_digitized')
    exif('Exif.Image.DateTime', 'Exif.Photo.OffsetTime', 'exif_modified')
    # Only recognizable camera-style names, not arbitrary six/eight-digit numbers.
    match = re.match(r'^(?:(?:IMG|PXL|DSC)[_-])?(\d{4})[-_]?(\d{2})[-_]?(\d{2})(?:[_ -](\d{2})(\d{2})(\d{2}))?(?=[_. -]|$)',
                     record['filename'], re.IGNORECASE)
    if match:
        try:
            parts = match.groups()
            dt = datetime(*(int(p) for p in parts[:3]), *(int(p or 0) for p in parts[3:]))
            add(dt.isoformat() if parts[3] else dt.date().isoformat(), 'filename', 'low')
        except ValueError as exc:
            warnings.append(f'Invalid filename date: {exc}')
    if use_mtime and record.get('mtime_epoch') is not None:
        try:
            add(datetime.fromtimestamp(record['mtime_epoch'], timezone.utc).isoformat(), 'filesystem_mtime', 'low')
        except (ValueError, OverflowError, OSError) as exc:
            warnings.append(f'Invalid filesystem date: {exc}')
    chosen = candidates[0] if candidates else dict(timestamp=None, source='unknown', confidence='unknown')
    return {**chosen, 'candidates': candidates, 'warnings': warnings}
