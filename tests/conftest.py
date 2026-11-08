import os
from pathlib import Path

import pytest
from PIL import Image


@pytest.fixture
def engine():
    path = Path(os.environ.get("DINNDEX_ENGINE", "build/dinndex")).resolve()
    if not path.is_file():
        pytest.fail(
            "Build the C++ engine first: cmake -S . -B build && cmake --build build"
        )
    return path


@pytest.fixture
def photo():
    def create(path, color="green", exif=None):
        path.parent.mkdir(parents=True, exist_ok=True)
        image = Image.new("RGB", (12, 8), color)
        image.save(path, **({"exif": exif} if exif is not None else {}))
        return path

    return create
