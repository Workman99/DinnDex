import json

import pytest

from dinndex.dates import reconcile
from dinndex.takeout import TakeoutIndex


def record(filename="unknown.jpg", **embedded):
    return dict(
        filename=filename, embedded_dates=embedded, mtime_epoch=1704067200
    )


def test_naive_exif_is_not_utc_and_invalid_dates_fall_through():
    r = record(**{"Exif.Photo.DateTimeOriginal": "2023:10:12 18:30:00"})
    date = reconcile(r, [])
    assert date["timestamp"] == "2023-10-12T18:30:00"
    r["embedded_dates"]["Exif.Photo.DateTimeOriginal"] = "2023:02:31 18:30:00"
    r["filename"] = "IMG_20240229_193000.jpg"
    date = reconcile(r, [])
    assert date["source"] == "filename"
    assert date["timestamp"] == "2024-02-29T19:30:00"
    assert date["warnings"]


def test_unknown_dates_and_opt_in_mtime():
    assert reconcile(record(), [])["timestamp"] is None
    date = reconcile(record(), [], use_mtime=True)
    assert (date["source"], date["confidence"]) == ("filesystem_mtime", "low")
    assert reconcile(record("IMG_20230229.jpg"), [])["timestamp"] is None
    assert reconcile(record("2024-01-02.jpg"), [])["timestamp"] == "2024-01-02"


@pytest.mark.parametrize(
    "value", ["garbage", {}, {"timestamp": "NaN"}, {"timestamp": "9" * 80}]
)
def test_bad_takeout_timestamp(value):
    date = reconcile(
        record(),
        [{"path": "sidecar.json", "metadata": {"photoTakenTime": value}}],
    )
    assert date["timestamp"] is None
    assert date["warnings"]


def test_takeout_matching_and_ambiguity(tmp_path):
    def sidecar(name, title):
        (tmp_path / name).write_text(
            json.dumps({"title": title, "photoTakenTime": {"timestamp": "1"}})
        )

    sidecar("truncated.supplemental-metadata.json", "long-name.jpg")
    sidecar("photo.jpg(1).json", "photo.jpg")
    sidecar("first.json", "ambiguous.jpg")
    sidecar("second.json", "ambiguous.jpg")
    sidecar("a.jpg.json", "a.jpg")
    sidecar("a.jpg.supplemental-metadata.json", "a.jpg")
    index = TakeoutIndex(tmp_path)
    assert len(index.match(tmp_path / "long-name.jpg")[0]) == 1
    assert len(index.match(tmp_path / "photo(1).jpg")[0]) == 1
    assert index.match(tmp_path / "ambiguous.jpg")[0] == []
    assert index.match(tmp_path / "ambiguous.jpg")[1]
    assert index.match(tmp_path / "a.jpg")[0] == []
    assert index.match(tmp_path / "unmatched.jpg") == ([], [])
