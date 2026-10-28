"""Build/install maxima2.exe, a Windows bridge to this checkout's Release build."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import shlex
import shutil
import subprocess


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", action="store_true", help="install and archive legacy Maxima launchers")
    args = parser.parse_args()
    project = Path(__file__).resolve().parent.parent
    engine = project / "build/cpp26/release/src/maxima2"
    if not engine.is_file():
        parser.error("build cpp26_release first")
    output = project / ".tools/windows/maxima2.exe"
    output.parent.mkdir(parents=True, exist_ok=True)
    command = ("LD_LIBRARY_PATH=" + shlex.quote(str(project / ".tools/cpp26/lib"))
               + " exec " + shlex.quote(str(engine)))
    subprocess.run(["x86_64-w64-mingw32-gcc", "-O2", "-Wall", "-Wextra", "-static",
                    "-DMAXIMA_ENGINE_COMMAND=L" + json.dumps(command),
                    str(project / "tools/windows_bridge.c"), "-o", str(output)], check=True)
    print(output, flush=True)
    if not args.install:
        return
    bundle = Path("/mnt/c/dev/maxima")
    entrypoints = Path("/mnt/c/dev/uci-engines")
    backup = project / ".context/releases" / datetime.now(timezone.utc).strftime("%Y-%m-%d-%H%M%S-maxima2")
    backup.mkdir(parents=True)
    manifest_path = bundle / "SHA256.json"
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    for folder in (bundle, entrypoints):
        folder.mkdir(parents=True, exist_ok=True)
        for name in ("maxima.exe", "maxima-new.exe", "maxima-hce.exe", "maxima2.exe"):
            old = folder / name
            if old.exists():
                shutil.copy2(old, backup / (folder.name + "-" + name))
        temporary = folder / "maxima2.exe.update-tmp"
        shutil.copy2(output, temporary)
        if sha256(temporary) != sha256(output):
            raise RuntimeError("launcher copy checksum mismatch")
        temporary.replace(folder / "maxima2.exe")
        for name in ("maxima.exe", "maxima-new.exe", "maxima-hce.exe"):
            (folder / name).unlink(missing_ok=True)
            manifest.pop(name, None)
    for name in ("README.txt", "SHA256.json"):
        if (bundle / name).exists():
            shutil.copy2(bundle / name, backup / name)
    readme = ("Maxima Windows GUI engine: C:\\dev\\maxima\\maxima2.exe\n\n"
              "Uses the current cpp26_release build through WSL Ubuntu.\n"
              "Rebuild that preset to update the GUI engine; no binary copy is needed.\n"
              f"Engine: {engine}\n"
              "Keep this checkout and .tools/cpp26 runtime installed.\n"
              "NNUE and book load from C:\\dev\\maxima.\n"
              "SyzygyPath: C:\\dev\\maxima\\syzygy (ProbeLimit=5, ProbeDepth=1).\n"
              "Windows paths for EvalFile, BookFile and SyzygyPath are translated.\n")
    (bundle / "README.txt").write_text(readme)
    for name in ("maxima2.exe", "README.txt"):
        manifest[name] = sha256(bundle / name)
    temporary = manifest_path.with_suffix(".json.update-tmp")
    temporary.write_text(json.dumps(manifest, indent=2) + "\n")
    temporary.replace(manifest_path)
    (backup / "installation.json").write_text(json.dumps({
        "engine": str(engine), "engine_sha256": sha256(engine),
        "launcher_sha256": sha256(output)}, indent=2) + "\n")
    print(f"Installed maxima2.exe; previous launchers archived in {backup}")


if __name__ == "__main__":
    main()
