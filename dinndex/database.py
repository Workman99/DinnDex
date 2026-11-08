"""SQLite connection and ordered, transactional schema migrations."""
import sqlite3
from importlib.resources import files
from pathlib import Path


def connect(path: Path | str) -> sqlite3.Connection:
    path = Path(path).expanduser()
    path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path)
    db.row_factory = sqlite3.Row
    db.execute('PRAGMA foreign_keys = ON')
    try:
        db.execute('PRAGMA journal_mode = WAL')
        migrate(db)
    except Exception:
        db.close()
        raise
    return db


def migrate(db: sqlite3.Connection) -> None:
    version = db.execute('PRAGMA user_version').fetchone()[0]
    migrations = sorted(files('dinndex').joinpath('migrations').iterdir(), key=lambda p: p.name)
    latest = len(migrations)
    if version > latest:
        raise ValueError('Database was created by a newer DinnDex version')
    for resource in migrations[version:]:
        number = int(resource.name.split('_')[0])
        if number != version + 1:
            raise ValueError('Nonconsecutive database migrations')
        try:
            db.executescript('BEGIN IMMEDIATE;\n' + resource.read_text(encoding='utf-8')
                             + f'\nPRAGMA user_version = {number};\nCOMMIT;')
        except Exception:
            db.rollback()
            raise
        version = number
