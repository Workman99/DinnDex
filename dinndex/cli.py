"""Command-line entry point; subprocesses use argument lists, never a shell."""

import argparse
import json
import os
import sqlite3
import sys
from pathlib import Path

from .database import connect
from .importer import import_directory


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="DinnDex — Your dinner history, indexed."
    )
    commands = parser.add_subparsers(dest="command", required=True)
    init = commands.add_parser(
        "init", help="Initialize or migrate the SQLite database"
    )
    imp = commands.add_parser(
        "import",
        help="Import a local photo directory without changing originals",
    )
    imp.add_argument("--source", type=Path, required=True)
    imp.add_argument(
        "--engine",
        type=Path,
        default=Path(os.environ.get("DINNDEX_ENGINE", "build/dinndex")),
    )
    imp.add_argument(
        "--use-mtime",
        action="store_true",
        help="Opt in to low-confidence filesystem date fallback",
    )
    stats = commands.add_parser("stats", help="Show archive record counts")
    for command in (init, imp, stats):
        command.add_argument(
            "--db", type=Path, default=Path("data/dinndex.sqlite3")
        )
    args = parser.parse_args(argv)
    db = None
    try:
        db = connect(args.db)
        if args.command == "import":
            result = import_directory(
                db, args.source, args.engine, args.use_mtime
            )
            print(json.dumps(result, ensure_ascii=False, indent=2))
            return 2 if result["failed"] else 0
        if args.command == "stats":
            print(
                json.dumps(
                    {
                        table: db.execute(
                            f"SELECT count(*) FROM {table}"
                        ).fetchone()[0]
                        for table in ("photos", "photo_sources", "meals")
                    }
                )
            )
        else:
            print(f"Database initialized: {args.db}")
        return 0
    except (OSError, ValueError, RuntimeError, sqlite3.Error) as exc:
        print(f"dinndex: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(
            "Import interrupted. Committed records are safe; rerun to resume.",
            file=sys.stderr,
        )
        return 130
    finally:
        if db is not None:
            db.close()
