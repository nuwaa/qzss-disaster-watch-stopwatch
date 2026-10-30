"""Build an allowlisted Raspberry Pi deployment archive without local secrets."""
from __future__ import annotations

from pathlib import Path
import tarfile


ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "outputs" / "qzss-watch-pi.tar.gz"


def main():
    paths = [ROOT / "pyproject.toml", ROOT / "deploy/raspberry-pi/qzss-watch.service"]
    paths.extend(sorted((ROOT / "src/qzss_watch").glob("*.py")))
    DEST.parent.mkdir(parents=True, exist_ok=True)
    with tarfile.open(DEST, "w:gz") as archive:
        for path in paths:
            archive.add(path, arcname=path.relative_to(ROOT).as_posix())
    print(DEST)


if __name__ == "__main__":
    main()
