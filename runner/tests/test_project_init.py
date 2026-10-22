"""maxima init: pinned book downloads, offline skips, and CI check mode."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from maxima_runner import download_tablebases, project_init


def archive(member, data):
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as bundle:
        bundle.writestr(member, data)
    return buffer.getvalue()


def options(**overrides):
    values = dict(check=False, build=False, debug=False, syzygy=0, solid=False, force=False)
    values.update(overrides)
    return argparse.Namespace(**values)


class init_tests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.data = b"polyglot entries"
        self.step = project_init.book_step("fire.bin", "https://mirror/Daring.zip", "Daring.bin",
                                           len(self.data), hashlib.sha256(self.data).hexdigest())

    def fetch(self, member="Daring.bin", data=None):
        payload = archive(member, self.data if data is None else data)
        return patch.object(project_init.urllib.request, "urlopen", side_effect=lambda *a, **k: io.BytesIO(payload))

    def test_downloads_then_skips_verified_book(self):
        with self.fetch():
            self.assertEqual(self.step(self.root, options()), ("done", "Daring.zip"))
        self.assertEqual((self.root / "fire.bin").read_bytes(), self.data)
        with patch.object(project_init.urllib.request, "urlopen") as request:
            self.assertEqual(self.step(self.root, options())[0], "ok")
            request.assert_not_called()

    def test_keeps_own_book_unless_forced(self):
        (self.root / "fire.bin").write_bytes(b"my own gambits")
        with patch.object(project_init.urllib.request, "urlopen") as request:
            self.assertIn("kept", self.step(self.root, options())[1])
            request.assert_not_called()
        with self.fetch():
            self.assertEqual(self.step(self.root, options(force=True))[0], "done")
        self.assertEqual((self.root / "fire.bin").read_bytes(), self.data)

    def test_rejects_wrong_member_or_checksum_without_leftovers(self):
        for member, data in (("Other.bin", None), ("Daring.bin", b"x" * len(self.data))):
            with self.fetch(member, data), self.assertRaises(ValueError):
                self.step(self.root, options())
            self.assertEqual(list(self.root.iterdir()), [])

    def test_check_mode_changes_nothing(self):
        with patch.object(project_init.urllib.request, "urlopen") as request:
            self.assertEqual(self.step(self.root, options(check=True))[0], "missing")
            request.assert_not_called()
        self.assertFalse((self.root / "fire.bin").exists())

    def test_syzygy_installed_offline(self):
        directory = self.root / "syzygy"
        directory.mkdir()
        names = [f"T{index}v.rtbw" for index in range(download_tablebases.table_counts[3])]
        for name in names:
            (directory / name).write_bytes(b"t")
        (directory / "manifest.json").write_text(json.dumps([{"file": name, "bytes": 1} for name in names]))
        self.assertTrue(download_tablebases.installed(directory, 3))
        self.assertFalse(download_tablebases.installed(directory, 4))
        (directory / names[0]).write_bytes(b"truncated?")
        self.assertFalse(download_tablebases.installed(directory, 3))

    def test_optional_steps_do_not_fail(self):
        self.assertEqual(project_init.solid_step(self.root, options())[0], "skipped")
        self.assertEqual(project_init.syzygy_step(self.root, options())[0], "skipped")
        self.assertEqual(project_init.network_step(self.root, options())[0], "skipped")


if __name__ == "__main__":
    unittest.main()
