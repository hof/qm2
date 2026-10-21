#!/usr/bin/env python3
"""Download a verified icsdrone-rs release for playing Maxima on FICS."""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import platform
import shutil
import tarfile
import tempfile
import urllib.request
import zipfile

from .project import project_root

repository = "jshriver/icsdrone-rs"
pinned_version = "v2.2.17"
targets = {
    ("linux", "x86_64"): "x86_64-unknown-linux-gnu",
    ("linux", "arm64"): "aarch64-unknown-linux-gnu",
    ("darwin", "x86_64"): "x86_64-apple-darwin",
    ("darwin", "arm64"): "aarch64-apple-darwin",
    ("windows", "x86_64"): "x86_64-pc-windows-msvc",
    ("windows", "arm64"): "aarch64-pc-windows-msvc",
}
machines = {"amd64": "x86_64", "x64": "x86_64", "aarch64": "arm64"}


def host_target():
    machine = platform.machine().lower()
    machine = machines.get(machine, machine)
    target = targets.get((platform.system().lower(), machine))
    if target is None:
        raise ValueError(f"no icsdrone-rs release for {platform.system()} {platform.machine()}; use --target")
    return target


def release_asset(version, target):
    path = "latest" if version == "latest" else "tags/" + version
    request = urllib.request.Request(f"https://api.github.com/repos/{repository}/releases/{path}",
                                     headers={"Accept": "application/vnd.github+json"})
    with urllib.request.urlopen(request, timeout=30) as response:
        release = json.loads(response.read().decode("utf-8"))
    tag = release["tag_name"]
    names = [f"icsdrone-rs-{tag}-{target}.{extension}" for extension in ("tar.gz", "zip")]
    for asset in release.get("assets", []):
        if asset["name"] in names:
            digest = asset.get("digest") or ""
            if not digest.startswith("sha256:") or len(digest) != 7 + 64:
                raise ValueError(f"{asset['name']}: release publishes no SHA-256 digest")
            return {"version": tag, "file": asset["name"], "bytes": asset["size"],
                    "url": asset["browser_download_url"], "sha256": digest[7:]}
    raise ValueError(f"release {tag} has no asset for {target}")


def download_archive(asset, directory):
    temporary = None
    try:
        digest = hashlib.sha256()
        size = 0
        with urllib.request.urlopen(asset["url"], timeout=30) as response:
            with tempfile.NamedTemporaryFile(dir=directory, prefix=asset["file"] + ".", delete=False) as output:
                temporary = Path(output.name)
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break
                    size += len(chunk)
                    digest.update(chunk)
                    output.write(chunk)
        if size != asset["bytes"]:
            raise ValueError(f"{asset['file']}: expected {asset['bytes']} bytes, received {size}")
        if digest.hexdigest() != asset["sha256"]:
            raise ValueError(f"{asset['file']}: SHA-256 does not match the release digest")
        return temporary
    except BaseException:
        if temporary is not None and temporary.exists():
            temporary.unlink()
        raise


def safe_member(name):
    path = PurePosixPath(name.replace("\\", "/"))
    if path.is_absolute() or ".." in path.parts or ":" in name:
        raise ValueError(f"archive member escapes the install directory: {name}")
    return path


def extract_archive(archive, file_name, destination):
    if file_name.endswith(".zip"):
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                safe_member(member.filename)
            bundle.extractall(destination)
        return
    with tarfile.open(archive, "r:gz") as bundle:
        members = bundle.getmembers()
        for member in members:
            safe_member(member.name)
            if not (member.isfile() or member.isdir()):
                raise ValueError(f"archive member is not a regular file: {member.name}")
        if hasattr(tarfile, "data_filter"):
            bundle.extractall(destination, members=members, filter="data")
        else:
            bundle.extractall(destination, members=members)


def install(asset, directory):
    """Install the release into directory/<version>; return the client binary path."""
    target = directory / asset["version"]
    manifest = target / "manifest.json"
    executable = "icsdrone-rs.exe" if asset["file"].endswith(".zip") else "icsdrone-rs"
    if manifest.is_file() and json.loads(manifest.read_text()).get("sha256") == asset["sha256"]:
        found = sorted(target.rglob(executable))
        if found:
            return found[0], False
    directory.mkdir(parents=True, exist_ok=True)
    archive = download_archive(asset, directory)
    staging = Path(tempfile.mkdtemp(dir=directory, prefix=asset["version"] + "."))
    try:
        extract_archive(archive, asset["file"], staging)
        found = sorted(staging.rglob(executable))
        if not found:
            raise ValueError(f"{asset['file']}: archive contains no {executable}")
        if os.name != "nt":
            found[0].chmod(found[0].stat().st_mode | 0o755)
        (staging / "manifest.json").write_text(json.dumps(asset, indent=2) + "\n")
        if target.exists():
            shutil.rmtree(target)
        staging.replace(target)
        return target / found[0].relative_to(staging), True
    finally:
        archive.unlink()
        if staging.exists():
            shutil.rmtree(staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--version", default=pinned_version,
                        help=f"release tag, or latest (default: {pinned_version})")
    parser.add_argument("--target", choices=sorted(set(targets.values())),
                        help="platform build to fetch (default: this machine)")
    parser.add_argument("--directory", type=Path, default=project_root() / ".tools" / "icsdrone-rs",
                        help="install root; each version gets its own subdirectory")
    parser.add_argument("--dry-run", action="store_true", help="show the selected asset without downloading")
    args = parser.parse_args()
    asset = release_asset(args.version, args.target or host_target())
    print(f"{asset['file']}: {asset['bytes'] / 1024**2:.2f} MiB, sha256 {asset['sha256']}")
    if args.dry_run:
        return
    binary, downloaded = install(asset, args.directory)
    print(f"{'Downloaded' if downloaded else 'Verified'} {asset['version']}")
    print(f"Ready: {binary.resolve()}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(f"Download failed: {error}")
