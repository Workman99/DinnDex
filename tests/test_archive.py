import hashlib
import json
import os
import shutil
import sqlite3
import subprocess
from pathlib import Path

import pytest
from PIL import Image

from dinndex.database import connect
from dinndex.importer import import_directory


def scan(engine, source, *extra):
    return subprocess.run(
        [str(engine), "scan", str(source), *map(str, extra)],
        capture_output=True,
        text=True,
    )


def test_scanner_nested_duplicates_and_json(tmp_path, photo, engine):
    source = tmp_path / "photos"
    original = photo(source / "nested" / 'dîner "one".JPG')
    duplicate = source / "same.jpeg"
    shutil.copyfile(original, duplicate)
    photo(source / "other.png", "blue")
    photo(source / "web.webp", "red")
    (source / "bad.jpg").write_text("not a photo")
    (source / "ignore.txt").write_text("ignored")
    (source / "unsupported.HEIC").write_bytes(b"unsupported")
    (source / "nested" / "loop").symlink_to(source, target_is_directory=True)
    (source / "link.jpg").symlink_to(original)
    before = {p: p.read_bytes() for p in source.rglob("*") if p.is_file()}
    result = scan(engine, source)
    assert result.returncode == 2, result.stderr
    records = [json.loads(line) for line in result.stdout.splitlines()]
    images = [r for r in records if r["type"] == "photo"]
    assert len(images) == 5
    assert records[-1]["duplicates"] == 1
    assert records[-1]["failed"] == 1
    good = [r for r in images if r["status"] == "ok"]
    assert all((r["width"], r["height"]) == (12, 8) for r in good)
    assert all(
        r["sha256"] == hashlib.sha256(Path(r["path"]).read_bytes()).hexdigest()
        for r in good
    )
    assert sum(r["duplicate_of"] is not None for r in good) == 1
    assert all(p.read_bytes() == content for p, content in before.items())


def test_incremental_preserves_edits_and_multiple_sources(
    tmp_path, engine, photo
):
    source = tmp_path / "photos"
    original = photo(source / "IMG_20240229_183045.jpg")
    shutil.copyfile(original, source / "renamed.jpg")
    with connect(tmp_path / "archive.db") as db:
        first = import_directory(db, source, engine)
        assert (first["new"], first["known"], first["failed"]) == (1, 1, 0)
        assert db.execute("SELECT count(*) FROM photos").fetchone()[0] == 1
        assert (
            db.execute("SELECT count(*) FROM photo_sources").fetchone()[0] == 2
        )
        assert db.execute("SELECT count(*) FROM meals").fetchone()[0] == 1
        db.execute(
            "UPDATE meals SET dish_name='Human correction', notes='Keep me', meal_date='2020-01-02'"
        )
        db.execute(
            "UPDATE photos SET capture_timestamp='2020-01-02', timestamp_source='manual'"
        )
        db.commit()
        second = import_directory(db, source, engine)
        assert (second["new"], second["known"]) == (0, 2)
        meal = db.execute("SELECT * FROM meals").fetchone()
        assert (meal["dish_name"], meal["notes"], meal["meal_date"]) == (
            "Human correction",
            "Keep me",
            "2020-01-02",
        )
        assert (
            db.execute("SELECT timestamp_source FROM photos").fetchone()[0]
            == "manual"
        )
        photo(source / "another.png", "red")
        third = import_directory(db, source, engine)
        assert (third["new"], third["known"]) == (1, 2)
    # Data survives a closed connection and schema initialization is repeatable.
    db = connect(tmp_path / "archive.db")
    assert db.execute("SELECT count(*) FROM photos").fetchone()[0] == 2
    assert db.execute("PRAGMA user_version").fetchone()[0] == 1
    db.close()


def test_takeout_and_exif_integration(tmp_path, photo, engine):
    source = tmp_path / "photos"
    exif = Image.Exif()
    # Pillow writes these into the EXIF IFD, as Exiv2 expects.
    exif[34665] = {36867: "2023:10:12 18:30:00", 36881: "-04:00"}
    photo(source / "exif.jpg", exif=exif)
    photo(source / "naive.jpg", "blue", exif=Image.Exif())
    photo(source / "takeout.png", "red")
    (source / "exif.jpg.json").write_text(
        json.dumps({"photoTakenTime": {"timestamp": "1704067200"}})
    )
    (source / "truncated-name.supplemental-metadata.json").write_text(
        json.dumps(
            {
                "title": "takeout.png",
                "photoTakenTime": {"timestamp": "1704067200"},
            }
        )
    )
    db = connect(tmp_path / "archive.db")
    result = import_directory(db, source, engine)
    assert result["failed"] == 0
    rows = {r["filename"]: r for r in db.execute("SELECT * FROM photos")}
    assert rows["exif.jpg"]["capture_timestamp"] == "2023-10-12T18:30:00-04:00"
    assert rows["exif.jpg"]["timestamp_source"] == "exif_original"
    assert (
        rows["takeout.png"]["capture_timestamp"] == "2024-01-01T00:00:00+00:00"
    )
    assert rows["naive.jpg"]["timestamp_source"] == "unknown"
    metadata = json.loads(
        db.execute(
            "SELECT observed_metadata FROM photo_sources WHERE path LIKE ?",
            ("%/exif.jpg",),
        ).fetchone()[0]
    )
    assert [c["source"] for c in metadata["date"]["candidates"]] == [
        "exif_original",
        "takeout",
    ]
    db.close()


def test_partial_scan_imports_valid_records(tmp_path, engine, photo):
    source = tmp_path / "photos"
    photo(source / "good.png")
    (source / "bad.jpg").write_bytes(b"corrupt")
    (source / "good.png.json").write_text("{bad json")
    db = connect(tmp_path / "archive.db")
    result = import_directory(db, source, engine)
    assert (result["new"], result["failed"]) == (1, 1)
    assert result["warnings"]
    assert (
        db.execute("SELECT timestamp_source FROM photos").fetchone()[0]
        == "unknown"
    )
    db.close()


def test_empty_missing_and_output_protection(tmp_path, engine, photo):
    source = tmp_path / "empty"
    source.mkdir()
    result = scan(engine, source)
    assert result.returncode == 0
    assert json.loads(result.stdout)["images"] == 0
    assert scan(engine, tmp_path / "missing").returncode == 1
    image = photo(tmp_path / "original.jpg")
    content = image.read_bytes()
    assert scan(engine, source, "--output", image).returncode == 1
    assert image.read_bytes() == content
    output = tmp_path / "manifest.jsonl"
    assert scan(engine, source, "--output", output).returncode == 0
    assert json.loads(output.read_text())["type"] == "summary"
    assert scan(engine, source, "--bogus", output).returncode == 1


def test_database_constraints_and_atomic_rollback(tmp_path):
    db = connect(tmp_path / "archive.db")
    assert db.execute("PRAGMA foreign_keys").fetchone()[0] == 1
    with pytest.raises(sqlite3.IntegrityError), db:
        db.execute(
            "INSERT INTO photos(sha256, filename, primary_path, size_bytes, timestamp_source, date_confidence) VALUES (?, 'x', 'x', 1, 'unknown', 'unknown')",
            ("a" * 64,),
        )
        db.execute("INSERT INTO meal_photos VALUES (999, 999, 1)")
    assert db.execute("SELECT count(*) FROM photos").fetchone()[0] == 0
    with pytest.raises(sqlite3.IntegrityError), db:
        db.execute(
            "INSERT INTO photos(sha256, filename, primary_path, size_bytes, timestamp_source, date_confidence) VALUES ('short', 'x', 'x', 1, 'unknown', 'unknown')"
        )
    db.execute("PRAGMA user_version = 999")
    with pytest.raises(ValueError):
        from dinndex.database import migrate

        migrate(db)
    db.close()


def test_cli_end_to_end(tmp_path, engine, photo):
    source = tmp_path / "photos"
    photo(source / "meal.png")
    database = tmp_path / "db.sqlite3"
    import sys

    cmd = [sys.executable, "-m", "dinndex"]
    result = subprocess.run(
        cmd
        + [
            "import",
            "--source",
            str(source),
            "--db",
            str(database),
            "--engine",
            str(engine),
        ],
        text=True,
        capture_output=True,
    )
    assert result.returncode == 0, result.stderr
    assert json.loads(result.stdout)["new"] == 1
    result = subprocess.run(
        cmd + ["stats", "--db", str(database)], text=True, capture_output=True
    )
    assert result.returncode == 0
    assert json.loads(result.stdout) == {
        "photos": 1,
        "photo_sources": 1,
        "meals": 1,
    }
    result = subprocess.run(
        cmd
        + [
            "import",
            "--source",
            str(source / "missing"),
            "--db",
            str(database),
        ],
        text=True,
        capture_output=True,
    )
    assert result.returncode == 1


@pytest.mark.skipif(
    os.name != "posix" or os.geteuid() == 0,
    reason="requires non-root POSIX permissions",
)
def test_permission_failures_continue(tmp_path, engine, photo):
    source = tmp_path / "photos"
    photo(source / "good.png")
    unreadable = photo(source / "no-read.jpg", "red")
    hidden = source / "no-entry"
    hidden.mkdir()
    unreadable.chmod(0)
    hidden.chmod(0)
    try:
        result = scan(engine, source)
        assert result.returncode == 2
        records = [json.loads(line) for line in result.stdout.splitlines()]
        assert records[-1]["failed"] == 2
        assert any(r.get("status") == "ok" for r in records)
    finally:
        unreadable.chmod(0o600)
        hidden.chmod(0o700)


def test_failed_engine_retains_committed_records_and_can_resume(
    tmp_path, engine, photo
):
    source = tmp_path / "photos"
    photo(source / "meal.png")
    manifest = scan(engine, source)
    record = manifest.stdout.splitlines()[0]
    # A small test engine simulates interruption after one emitted photo.
    fake_engine = tmp_path / "interrupted-engine"
    fake_engine.write_text(
        "#!/usr/bin/env python3\nprint("
        + repr(record)
        + ")\nraise SystemExit(1)\n"
    )
    fake_engine.chmod(0o700)
    db = connect(tmp_path / "archive.db")
    with pytest.raises(RuntimeError, match="Engine failed"):
        import_directory(db, source, fake_engine)
    assert db.execute("SELECT count(*) FROM photos").fetchone()[0] == 1
    assert import_directory(db, source, engine)["known"] == 1
    db.close()


def test_same_date_does_not_merge_distinct_meals(tmp_path, engine, photo):
    source = tmp_path / "photos"
    photo(source / "2024-01-02_a.png", "red")
    photo(source / "2024-01-02_b.png", "blue")
    db = connect(tmp_path / "archive.db")
    assert import_directory(db, source, engine)["new"] == 2
    assert [r[0] for r in db.execute("SELECT meal_date FROM meals")] == [
        "2024-01-02",
        "2024-01-02",
    ]
    db.close()
