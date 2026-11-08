CREATE TABLE photos (
    id INTEGER PRIMARY KEY,
    sha256 TEXT NOT NULL UNIQUE CHECK(length(sha256) = 64),
    filename TEXT NOT NULL,
    primary_path TEXT NOT NULL,
    size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
    width INTEGER CHECK(width > 0),
    height INTEGER CHECK(height > 0),
    capture_timestamp TEXT,
    timestamp_source TEXT NOT NULL,
    date_confidence TEXT NOT NULL CHECK(date_confidence IN ('high', 'medium', 'low', 'unknown')),
    imported_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
);
CREATE TABLE photo_sources (
    id INTEGER PRIMARY KEY,
    photo_id INTEGER NOT NULL REFERENCES photos(id),
    path TEXT NOT NULL,
    source_type TEXT NOT NULL DEFAULT 'local',
    observed_metadata TEXT NOT NULL,
    last_observed_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    UNIQUE(photo_id, path)
);
CREATE TABLE meals (
    id INTEGER PRIMARY KEY,
    meal_date TEXT,
    dish_name TEXT NOT NULL DEFAULT '',
    description TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',
    dining_setting TEXT,
    created_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now')),
    updated_at TEXT NOT NULL DEFAULT (strftime('%Y-%m-%dT%H:%M:%fZ', 'now'))
);
CREATE TABLE meal_photos (
    meal_id INTEGER NOT NULL REFERENCES meals(id),
    photo_id INTEGER NOT NULL REFERENCES photos(id),
    is_primary INTEGER NOT NULL DEFAULT 1 CHECK(is_primary IN (0, 1)),
    PRIMARY KEY(meal_id, photo_id)
);
CREATE UNIQUE INDEX one_primary_photo_per_meal ON meal_photos(meal_id) WHERE is_primary = 1;
CREATE INDEX photos_capture_timestamp ON photos(capture_timestamp);
CREATE INDEX meals_date ON meals(meal_date);
CREATE INDEX sources_path ON photo_sources(path);
CREATE INDEX meal_photos_photo ON meal_photos(photo_id);
