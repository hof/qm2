import hashlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from maxima_runner import download_tablebases as downloader

class download_tests(unittest.TestCase):
    def test_selects_complete_sets(self):
        names = ["KRvK", "KBNvK", "KBBNvK", "KBBBBvK", "KBBBBBvK"]
        digests = "\n".join("a" * 64 + "  " + name + "." + extension
                            for name in names for extension in ("rtbw", "rtbz"))

        def response(url, **kwargs):
            if url.endswith("sha256"):
                return io.BytesIO(digests.encode())
            if url.endswith("7/"):
                return io.BytesIO(b'<a href="6v1_pawnless/">tables</a>')
            if url.endswith("6v1_pawnless/"):
                subset = names[4:]
                extensions = ("rtbw", "rtbz")
            else:
                subset = names[3:4] if "/6-" in url else names[:3]
                extensions = ("rtbw",) if url.endswith("wdl/") else ("rtbz",)
            listing = "\n".join(f'<a href="{name}.{extension}">{name}</a> 01-Jan-2000 00:00 42'
                                for name in subset for extension in extensions)
            return io.BytesIO(listing.encode())

        with patch.object(downloader.urllib.request, "urlopen", side_effect=response):
            for pieces in range(3, 8):
                tables = downloader.list_tables("https://mirror/", pieces)
                self.assertEqual(len(tables), (pieces - 2) * 2)
                self.assertTrue(all(len(table["file"].split(".")[0].replace("v", "")) <= pieces
                                    for table in tables))

    def test_verifies_and_retries_corrupt_files(self):
        data = b"table data"
        table = {"file": "KRvK.rtbw", "bytes": len(data), "url": "https://mirror/KRvK.rtbw",
                 "sha256": hashlib.sha256(data).hexdigest()}
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            with patch.object(downloader.urllib.request, "urlopen", return_value=io.BytesIO(data)):
                entry, downloaded = downloader.download_table(table, directory)
                self.assertTrue(downloaded)
                self.assertEqual(entry, table)
            with patch.object(downloader.urllib.request, "urlopen") as request:
                self.assertFalse(downloader.download_table(table, directory)[1])
                request.assert_not_called()
            (directory / table["file"]).write_bytes(b"wrong data")
            with patch.object(downloader.urllib.request, "urlopen", return_value=io.BytesIO(data)):
                self.assertTrue(downloader.download_table(table, directory)[1])
            self.assertEqual((directory / table["file"]).read_bytes(), data)

    def test_failed_download_preserves_old_file(self):
        data = b"correct"
        table = {"file": "KRvK.rtbw", "bytes": len(data), "url": "https://mirror/KRvK.rtbw",
                 "sha256": hashlib.sha256(data).hexdigest()}
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            target = directory / table["file"]
            target.write_bytes(b"old file")
            for bad_data in (b"short", b"corrupt"):
                with patch.object(downloader.urllib.request, "urlopen", return_value=io.BytesIO(bad_data)):
                    with self.assertRaises(ValueError):
                        downloader.download_table(table, directory)
                self.assertEqual(target.read_bytes(), b"old file")
                self.assertEqual(list(directory.iterdir()), [target])

    def test_manifest_is_sorted_and_atomic(self):
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "manifest.json"
            downloader.save_manifest(path, {"b": {"file": "b"}, "a": {"file": "a"}})
            self.assertEqual(downloader.json.loads(path.read_text()), [{"file": "a"}, {"file": "b"}])
            self.assertFalse(path.with_suffix(".tmp").exists())


if __name__ == "__main__":
    unittest.main()
