import hashlib
import io
import json
from pathlib import Path
import tarfile
import tempfile
import unittest
from unittest.mock import patch

from maxima_runner import download_icsdrone as downloader


def archive_bytes(members):
    buffer = io.BytesIO()
    with tarfile.open(fileobj=buffer, mode="w:gz") as bundle:
        for name, data in members.items():
            info = tarfile.TarInfo(name)
            info.size = len(data)
            bundle.addfile(info, io.BytesIO(data))
    return buffer.getvalue()


def asset_for(data, version="v1.0.0"):
    return {"version": version, "file": f"icsdrone-rs-{version}-x86_64-unknown-linux-gnu.tar.gz",
            "bytes": len(data), "url": "https://example/asset", "sha256": hashlib.sha256(data).hexdigest()}


class download_tests(unittest.TestCase):
    def test_selects_host_asset_with_digest(self):
        release = {"tag_name": "v1.0.0", "assets": [
            {"name": "icsdrone-rs-v1.0.0-aarch64-unknown-linux-gnu.tar.gz", "size": 1,
             "browser_download_url": "https://example/arm", "digest": "sha256:" + "b" * 64},
            {"name": "icsdrone-rs-v1.0.0-x86_64-unknown-linux-gnu.tar.gz", "size": 2,
             "browser_download_url": "https://example/x86", "digest": "sha256:" + "a" * 64}]}
        with patch.object(downloader.urllib.request, "urlopen",
                          return_value=io.BytesIO(json.dumps(release).encode())) as request:
            asset = downloader.release_asset("v1.0.0", "x86_64-unknown-linux-gnu")
        self.assertTrue(request.call_args.args[0].full_url.endswith("/releases/tags/v1.0.0"))
        self.assertEqual(asset["url"], "https://example/x86")
        self.assertEqual(asset["sha256"], "a" * 64)
        del release["assets"][1]["digest"]
        with patch.object(downloader.urllib.request, "urlopen",
                          return_value=io.BytesIO(json.dumps(release).encode())):
            with self.assertRaises(ValueError):
                downloader.release_asset("v1.0.0", "x86_64-unknown-linux-gnu")

    def test_maps_host_platforms(self):
        for system, machine, target in (("Linux", "x86_64", "x86_64-unknown-linux-gnu"),
                                        ("Linux", "aarch64", "aarch64-unknown-linux-gnu"),
                                        ("Darwin", "arm64", "aarch64-apple-darwin"),
                                        ("Windows", "AMD64", "x86_64-pc-windows-msvc")):
            with patch.object(downloader.platform, "system", return_value=system), \
                    patch.object(downloader.platform, "machine", return_value=machine):
                self.assertEqual(downloader.host_target(), target)
        with patch.object(downloader.platform, "system", return_value="Plan9"):
            with self.assertRaises(ValueError):
                downloader.host_target()

    def test_installs_verifies_and_reuses(self):
        data = archive_bytes({"icsdrone-rs-v1.0.0/icsdrone-rs": b"binary",
                              "icsdrone-rs-v1.0.0/README.md": b"readme"})
        asset = asset_for(data)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with patch.object(downloader.urllib.request, "urlopen", return_value=io.BytesIO(data)):
                binary, downloaded = downloader.install(asset, directory)
            self.assertTrue(downloaded)
            self.assertEqual(binary, directory / "v1.0.0/icsdrone-rs-v1.0.0/icsdrone-rs")
            self.assertEqual(binary.read_bytes(), b"binary")
            with patch.object(downloader.urllib.request, "urlopen") as request:
                self.assertEqual(downloader.install(asset, directory), (binary, False))
                request.assert_not_called()
            self.assertEqual(sorted(path.name for path in directory.iterdir()), ["v1.0.0"])

    def test_corrupt_download_keeps_existing_install(self):
        data = archive_bytes({"icsdrone-rs": b"binary"})
        asset = asset_for(data)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with patch.object(downloader.urllib.request, "urlopen", return_value=io.BytesIO(data)):
                binary, _ = downloader.install(asset, directory)
            changed = {**asset, "sha256": "c" * 64}
            with patch.object(downloader.urllib.request, "urlopen", return_value=io.BytesIO(data)):
                with self.assertRaises(ValueError):
                    downloader.install(changed, directory)
            self.assertEqual(binary.read_bytes(), b"binary")
            self.assertEqual(sorted(path.name for path in directory.iterdir()), ["v1.0.0"])

    def test_rejects_unsafe_archive_members(self):
        for name in ("../icsdrone-rs", "/tmp/icsdrone-rs"):
            data = archive_bytes({name: b"binary"})
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                with patch.object(downloader.urllib.request, "urlopen", return_value=io.BytesIO(data)):
                    with self.assertRaises(ValueError):
                        downloader.install(asset_for(data), directory)
                self.assertEqual(list(directory.iterdir()), [])


if __name__ == "__main__":
    unittest.main()
