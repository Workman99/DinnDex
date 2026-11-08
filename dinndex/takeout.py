"""Directory-local Takeout sidecar indexing; ambiguous matches are never guessed."""
import json
import re
from pathlib import Path
from typing import Any


class TakeoutIndex:
    def __init__(self, directory: Path):
        self.by_title: dict[str, list[tuple[Path, dict[str, Any]]]] = {}
        self.by_name: dict[str, tuple[Path, dict[str, Any]]] = {}
        self.invalid: dict[str, str] = {}
        self.diagnostics: list[str] = []
        for path in sorted(directory.glob('*.json')):
            if path.is_symlink():
                continue
            try:
                # Sidecars should be small; don't load an arbitrary giant JSON export.
                if path.stat().st_size > 4 * 1024 * 1024:
                    raise ValueError('sidecar exceeds 4 MiB')
                data = json.loads(path.read_text(encoding='utf-8'))
                if not isinstance(data, dict):
                    raise ValueError('expected a JSON object')
                self.by_name[path.name] = (path, data)
                title = data.get('title')
                if isinstance(title, str):
                    self.by_title.setdefault(title, []).append((path, data))
            except (OSError, ValueError) as exc:
                self.invalid[path.name] = f'{path}: {exc}'
                self.diagnostics.append(self.invalid[path.name])

    def match(self, photo: Path) -> tuple[list[dict[str, Any]], list[str]]:
        names = [photo.name + '.json', photo.name + '.supplemental-metadata.json']
        # Takeout may move the copy counter after the JSON suffix: foo.jpg(1).json.
        counter = re.fullmatch(r'(.+)\((\d+)\)(\.[^.]+)', photo.name)
        if counter:
            base, number, ext = counter.groups()
            names += [f'{base}{ext}({number}).json',
                      f'{base}{ext}.supplemental-metadata({number}).json']
        exact = [self.by_name[name] for name in names if name in self.by_name]
        candidates = exact or self.by_title.get(photo.name, [])
        errors = [self.invalid[name] for name in names if name in self.invalid]
        if len(candidates) > 1:
            errors.append(f'Ambiguous Takeout sidecars for {photo.name}; no date selected')
            return [], errors
        return [dict(path=str(p), metadata=d) for p, d in candidates], errors
