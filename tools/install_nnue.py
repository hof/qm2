#!/usr/bin/env python3
"""Verify and install the bundled native network without network access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import tempfile


def main():
    root = Path(__file__).resolve().parents[1]
    metadata = json.loads((root / "networks/maxima-200m.json").read_text())
    source = root / "networks" / metadata["name"]
    if (source.stat().st_size != metadata["bytes"]
            or hashlib.sha256(source.read_bytes()).hexdigest() != metadata["sha256"]):
        raise ValueError("Bundled Maxima network checksum mismatch")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=root / metadata["name"])
    destination = parser.parse_args().output
    if (destination.is_file()
            and hashlib.sha256(destination.read_bytes()).hexdigest() == metadata["sha256"]):
        print(destination)
        return
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            with source.open("rb") as stream:
                shutil.copyfileobj(stream, output)
            output.flush()
            os.fsync(output.fileno())
        if hashlib.sha256(temporary.read_bytes()).hexdigest() != metadata["sha256"]:
            raise ValueError("Installed Maxima network checksum mismatch")
        os.replace(temporary, destination)
        print(destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


if __name__ == "__main__":
    main()
