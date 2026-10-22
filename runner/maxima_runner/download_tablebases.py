#!/usr/bin/env python3
"""Download complete local Syzygy sets without adding networking to the engine."""

import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import re
import tempfile
import urllib.request

from .project import project_root


def list_tables(base_url, pieces):
    base_url = base_url.rstrip("/") + "/"
    directories = ["3-4-5-wdl/", "3-4-5-dtz/"]
    if pieces >= 6:
        directories += ["6-wdl/", "6-dtz/"]
    if pieces == 7:
        with urllib.request.urlopen(base_url + "7/", timeout=30) as response:
            listing = response.read().decode("utf-8")
        directories += ["7/" + directory for directory in
                        re.findall(r'href="([1-6]v[1-6]_pawn(?:ful|less)/)"', listing)]
        if len(directories) == 4:
            raise ValueError("Mirror did not list seven-piece tables")
    with urllib.request.urlopen(base_url + "sha256", timeout=30) as response:
        checksums = dict((name, digest) for digest, name in
                         re.findall(r"^([0-9a-f]{64})  (K[QRBNP]*vK[QRBNP]*\.rtb[wz])$",
                                    response.read().decode("utf-8"), re.MULTILINE))
    tables = []
    for directory in directories:
        url = base_url + directory
        with urllib.request.urlopen(url, timeout=30) as response:
            listing = response.read().decode("utf-8")
        for line in listing.splitlines():
            match = re.search(r'href="(K[QRBNP]*vK[QRBNP]*\.rtb[wz])".*?\s(\d+)\s*$', line)
            if not match:
                continue
            name, size = match.groups()
            if len(name.split(".")[0].replace("v", "")) <= pieces:
                if name not in checksums:
                    raise ValueError(f"Mirror has no published checksum for {name}")
                tables.append({"file": name, "bytes": int(size), "url": url + name,
                               "sha256": checksums[name]})
    names = {table["file"] for table in tables}
    if not tables or len(names) != len(tables):
        raise ValueError("Download server returned an empty or duplicate file listing")
    if {name[:-1] for name in names if name.endswith("w")} != {name[:-1] for name in names if name.endswith("z")}:
        raise ValueError("Mirror did not list matching WDL and DTZ sets")
    expected = {name for name in checksums if len(name.split(".")[0].replace("v", "")) <= pieces}
    if names != expected:
        raise ValueError("Mirror listing is incomplete for the selected piece count")
    return sorted(tables, key=lambda table: table["file"])


def file_checksum(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download_table(table, directory):
    target = directory / table["file"]
    if target.is_file() and target.stat().st_size == table["bytes"]:
        digest = file_checksum(target)
        if digest == table["sha256"]:
            return {**table, "sha256": digest}, False
    temporary = None
    try:
        digest = hashlib.sha256()
        size = 0
        with urllib.request.urlopen(table["url"], timeout=30) as response:
            with tempfile.NamedTemporaryFile(dir=directory, prefix=target.name + ".", delete=False) as output:
                temporary = Path(output.name)
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    size += len(chunk)
                    digest.update(chunk)
                    output.write(chunk)
        if size != table["bytes"]:
            raise ValueError(f"{target.name}: expected {table['bytes']} bytes, received {size}")
        if digest.hexdigest() != table["sha256"]:
            raise ValueError(f"{target.name}: SHA-256 does not match the mirror's published checksum")
        temporary.replace(target)
        return {**table, "sha256": digest.hexdigest()}, True
    finally:
        if temporary is not None:
            if temporary.exists():
                temporary.unlink()


def save_manifest(path, entries):
    temporary = path.with_suffix(".tmp")
    temporary.write_text(json.dumps(sorted(entries.values(), key=lambda item: item["file"]), indent=2) + "\n")
    temporary.replace(path)


# Complete Syzygy sets by maximum piece count; the tables never change.
table_counts = {3: 10, 4: 70, 5: 290, 6: 1020, 7: 3022}


def installed(directory, pieces):
    """Offline check: the manifest lists the complete set and every file has its size."""
    manifest = directory / "manifest.json"
    if not manifest.is_file():
        return False
    entries = [entry for entry in json.loads(manifest.read_text())
               if len(entry["file"].split(".")[0].replace("v", "")) <= pieces]
    return len(entries) == table_counts[pieces] and all(
        (directory / entry["file"]).is_file() and (directory / entry["file"]).stat().st_size == entry["bytes"]
        for entry in entries)


def download(pieces, directory, base_url="https://tablebase.lichess.ovh/tables/standard", dry_run=False):
    tables = list_tables(base_url, pieces)
    total = sum(table["bytes"] for table in tables)
    amount = total / 1024**2
    unit = "MiB"
    if amount >= 1024:
        amount /= 1024
        unit = "GiB"
    if amount >= 1024:
        amount /= 1024
        unit = "TiB"
    print(f"Up to {pieces} pieces: {len(tables)} files, {amount:.2f} {unit}")
    if dry_run:
        return
    directory.mkdir(parents=True, exist_ok=True)
    manifest = directory / "manifest.json"
    entries = {entry["file"]: entry for entry in json.loads(manifest.read_text())} if manifest.exists() else {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(download_table, table, directory) for table in tables]
        for future in concurrent.futures.as_completed(futures):
            entry, downloaded = future.result()
            entries[entry["file"]] = entry
            save_manifest(manifest, entries)
            print(f"{'Downloaded' if downloaded else 'Verified'} {entry['file']}")
    print(f"Ready: {directory.resolve()}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pieces", type=int, choices=(3, 4, 5, 6, 7), default=4,
                        help="maximum piece count, including kings (default: 4)")
    parser.add_argument("--directory", type=Path,
                        default=project_root() / "tablebases" / "syzygy")
    parser.add_argument("--dry-run", action="store_true", help="show size without downloading table files")
    parser.add_argument("--base-url", default="https://tablebase.lichess.ovh/tables/standard",
                        help="Syzygy mirror containing piece-count WDL and DTZ directories")
    args = parser.parse_args()
    download(args.pieces, args.directory, args.base_url, args.dry_run)

if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(f"Download failed: {error}")
