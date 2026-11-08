# Milestone 1 architecture and contracts

## C++/Python boundary

`dinndex scan SOURCE [--output NEW_FILE]` emits UTF-8 JSON Lines. Every record has
`schema_version: 1` and `type`. Python rejects unknown versions/types. Diagnostics
and progress go to stderr, never into the manifest. Python invokes the engine with
an argument vector and never a shell. Test and CLI engine paths are configurable.

A `photo` record contains `path` (absolute), `filename`, `extension`, `size_bytes`,
`sha256` (lowercase hex), nullable `width`/`height`, `mtime_epoch` (UTC epoch seconds),
`embedded_dates` (raw EXIF keys/values), nullable `duplicate_of` (first valid path
with the same hash in this scan), `status` (`ok`/`error`), and `errors` (strings).
Dates are reconciled in Python; C++ never pretends a raw naive EXIF date is UTC.
Metadata extraction failure marks a photo as failed; the importer does not persist
it. Exiv2 provides format recognition, dimensions, and metadata without decoding
all pixels. Known-format header acceptance does not prove every pixel is intact.

A traversal `error` contains `path` and `message`. A final `summary` contains
`images`, `duplicates`, `failed`, and `skipped`. `images` includes failed candidate
images; `failed` also includes traversal errors, so may exceed `images`. `skipped`
counts nonimages and symlinks (including unsupported HEIC). Regular files with
supported extensions are processed regardless of case; all symlinks below the
source root are ignored. Traversal order and the first duplicate path are unspecified.
C++ checks size/mtime before and after processing to detect ordinary concurrent
changes, but does not provide a filesystem snapshot or protection against a
malicious concurrent writer. Import an archive that is not actively changing.

SHA-256 uses OpenSSL EVP with a 64 KiB buffer. An unordered map identifies exact
in-scan duplicates; SQLite's unique hash constraint provides cross-import identity.
This is different from visual similarity, which is a future milestone.

## Persistence

`photos` stores immutable content identity and the initially chosen metadata.
`photo_sources` is unique on (photo_id, path), recording observations/provenance.
If a file at a previously observed path changes contents, the new hash becomes a
new photo; the old observation remains historical. Primary paths are the first
observed paths, not managed copies; moving originals later may make them stale.
DinnDex does not copy or own original files and does not garbage-collect records.

`meals` holds editable application data separately from file observations.
`meal_photos` permits multiple photos per meal and multiple meals on one date;
a partial unique index limits each meal to one primary photo. Initially there is
one meal per unique photo. No later UI/AI tables are created speculatively: tags,
annotations, and embeddings can be added via migrations without changing hashes
or the C++ protocol.

All connections enable foreign keys and WAL. Packaged numbered migrations run
inside explicit transactions and advance `PRAGMA user_version`. Newer unknown
versions are rejected. Each photo/source/initial-meal insertion is one transaction;
committed records survive a later engine failure or interrupted import. Duplicate
photos only refresh source observations. They never replace existing metadata or
meal edits. Full import rollback is intentionally not used, to support resumability.

SQLite stores capture timestamps as ISO 8601 text (date-only, naive, or offset-aware)
with explicit source/confidence; these are not interchangeable UTC instants.
Calendar browsing should use meal dates, not sort mixed offset strings as absolute
instants. A future editing API must update `updated_at` and explicitly identify
manual changes. The absence of a photograph says nothing about whether dinner was eaten.

## Takeout matching and dates

Sidecars are indexed once per encountered image directory. Exact `image.ext.json`
and `image.ext.supplemental-metadata.json` names take priority; moved duplicate
counters are recognized. Otherwise an exact `title` match handles truncated or
renamed sidecar filenames. Multiple candidates at the chosen priority are ambiguous
and produce no Takeout date. Cross-directory and fuzzy matches are deliberately
not guessed. Symlinked sidecars are skipped. Sidecars over 4 MiB are rejected;
malformed JSON and malformed timestamps become warnings. All matched raw metadata
and competing date candidates are stored; no GPS data is emitted by the C++ engine,
but raw private Takeout provenance may contain it in the local database.

See README for precedence. Metadata conflicts are preserved rather than combined
into a manufactured timezone or timestamp. Later date correction/reconciliation
must be explicit, since repeat imports preserve chosen values and user edits.

## Scope and validation

This milestone has no UI, embeddings, AI calls, search, or analytics dashboard.
The count-only stats command supports inspection. The CLI and SQLite API form the
integration point for later application work. Tests run without credentials or
network calls and generate all photos synthetically. Unit and end-to-end checks
cover current behavior; no benchmarks or later-milestone guarantees are claimed.
