#!/usr/bin/env python3
"""Download the pinned CC0 bootstrap network; verify before replacing a file."""

import hashlib
import os
from pathlib import Path
import tempfile
from urllib.request import urlopen


network_name = "nn-62ef826d1a6d.nnue"
network_sha256 = "62ef826d1a6d11b9e814188025aa02a60815c037292e0ef9bbb9bf4f724e5e63"
network_url = "https://raw.githubusercontent.com/official-stockfish/networks/master/" + network_name


def main():
    destination = Path(__file__).resolve().parents[1] / "networks" / network_name
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and hashlib.sha256(destination.read_bytes()).hexdigest() == network_sha256:
        print(destination)
        return
    temporary = None
    try:
        with urlopen(network_url, timeout=60) as response, tempfile.NamedTemporaryFile(
                dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            digest = hashlib.sha256()
            size = 0
            while True:
                block = response.read(65536)
                if not block:
                    break
                size += len(block)
                if size > 21022697:
                    raise ValueError("Network exceeds the pinned size")
                digest.update(block)
                output.write(block)
        if size != 21022697 or digest.hexdigest() != network_sha256:
            raise ValueError("Network checksum or size mismatch")
        os.replace(temporary, destination)
        print(destination)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


if __name__ == "__main__":
    main()
