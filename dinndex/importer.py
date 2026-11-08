"""Streaming C++/Python boundary and incremental, content-addressed persistence."""
import json
import sqlite3
import subprocess
import tempfile
import sys
from pathlib import Path
from typing import Any
from .dates import reconcile
from .takeout import TakeoutIndex


def store_record(db: sqlite3.Connection, record: dict[str, Any], date: dict[str, Any],
                 sidecars: list[dict[str, Any]], warnings: list[str]) -> bool:
    """Commit one content record and its provenance atomically; never rewrite meal edits."""
    metadata = json.dumps(dict(scanner=record, date=date, sidecars=sidecars, warnings=warnings), ensure_ascii=False)
    with db:
        cursor = db.execute('''INSERT INTO photos
            (sha256, filename, primary_path, size_bytes, width, height, capture_timestamp,
             timestamp_source, date_confidence) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)
            ON CONFLICT(sha256) DO NOTHING''',
            (record['sha256'], record['filename'], record['path'], record['size_bytes'],
             record['width'], record['height'], date['timestamp'], date['source'], date['confidence']))
        created = cursor.rowcount == 1
        photo_id = db.execute('SELECT id FROM photos WHERE sha256 = ?', (record['sha256'],)).fetchone()[0]
        db.execute('''INSERT INTO photo_sources(photo_id, path, observed_metadata) VALUES (?, ?, ?)
            ON CONFLICT(photo_id, path) DO UPDATE SET observed_metadata=excluded.observed_metadata,
            last_observed_at=strftime('%Y-%m-%dT%H:%M:%fZ', 'now')''', (photo_id, record['path'], metadata))
        if created:
            cursor = db.execute('INSERT INTO meals(meal_date) VALUES (?)',
                                (date['timestamp'][:10] if date['timestamp'] else None,))
            db.execute('INSERT INTO meal_photos(meal_id, photo_id) VALUES (?, ?)', (cursor.lastrowid, photo_id))
    return created


def import_directory(db: sqlite3.Connection, source: Path, engine: Path,
                     use_mtime: bool = False) -> dict[str, Any]:
    source = source.expanduser().resolve(strict=True)
    if not source.is_dir():
        raise ValueError('Source must be a directory')
    engine = engine.expanduser().resolve(strict=True)
    indexes: dict[Path, TakeoutIndex] = {}
    summary: dict[str, Any] = dict(new=0, known=0, failed=0, warnings=[], scanner=None)
    # A file for stderr avoids pipe deadlocks, even for many metadata diagnostics.
    with tempfile.TemporaryFile(mode='w+', encoding='utf-8') as errors:
        process = subprocess.Popen([str(engine), 'scan', str(source)], stdout=subprocess.PIPE,
                                   stderr=errors, text=True, encoding='utf-8')
        try:
            assert process.stdout is not None
            for line in process.stdout:
                record = json.loads(line)
                if record.get('schema_version') != 1:
                    raise ValueError('Unsupported engine manifest schema')
                kind = record.get('type')
                if kind == 'summary':
                    summary['scanner'] = record
                    continue
                if kind == 'error' or record.get('status') == 'error':
                    summary['failed'] += 1
                    summary['warnings'].append(record)
                    continue
                if kind != 'photo':
                    raise ValueError('Unknown engine record type')
                path = Path(record['path'])
                if not path.is_absolute() or not path.is_relative_to(source):
                    raise ValueError('Engine returned a path outside source')
                if path.parent not in indexes:
                    indexes[path.parent] = TakeoutIndex(path.parent)
                    summary['warnings'].extend(indexes[path.parent].diagnostics)
                sidecars, warnings = indexes[path.parent].match(path)
                date = reconcile(record, sidecars, use_mtime)
                warnings += date['warnings']
                summary['warnings'].extend(warnings)
                created = store_record(db, record, date, sidecars, warnings)
                summary['new' if created else 'known'] += 1
                processed = summary['new'] + summary['known']
                if processed % 100 == 0:
                    print(f'Imported/observed {processed} valid files...', file=sys.stderr)
            code = process.wait()
            if code not in (0, 2) or summary['scanner'] is None:
                errors.seek(0)
                raise RuntimeError(f'Engine failed ({code}): {errors.read()[-4000:]}')
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait()
            if process.stdout:
                process.stdout.close()
    return summary
