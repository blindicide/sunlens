"""Проверки безопасного получения ядра без внешних сетевых запросов."""

import hashlib
import importlib.util
import io
import pathlib
import tarfile
import tempfile
import unittest

root = pathlib.Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("fetch_core", root / "tools/fetch-core.py")
fetch_core = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fetch_core)


class CoreAcquisitionTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.destination = pathlib.Path(self.temporary.name) / "cores"
        self.content = b"fixture-only-not-an-executable"
        self.member = "fixture/sing-box"
        self.archive = self.make_archive()
        self.asset = {
            "url": "https://example.invalid/core.tar.gz",
            "archiveSha256": hashlib.sha256(self.archive).hexdigest(),
            "binaryMember": self.member,
            "binarySha256": hashlib.sha256(self.content).hexdigest(),
        }

    def make_archive(self, symlink=False):
        buffer = io.BytesIO()
        with tarfile.open(fileobj=buffer, mode="w:gz") as tar:
            member = tarfile.TarInfo(self.member)
            if symlink:
                member.type = tarfile.SYMTYPE
                member.linkname = "/etc/passwd"
                tar.addfile(member)
            else:
                member.size = len(self.content)
                tar.addfile(member, io.BytesIO(self.content))
            unwanted = tarfile.TarInfo("../../unwanted")
            unwanted.size = 1
            tar.addfile(unwanted, io.BytesIO(b"x"))
        return buffer.getvalue()

    def opener(self, url, timeout):
        self.assertEqual(url, self.asset["url"])
        self.assertEqual(timeout, 30)
        response = io.BytesIO(self.archive)
        response.status = 200
        response.url = url
        return response

    def test_verified_acquisition_and_cached_binary(self):
        binary = fetch_core.acquire(self.asset, self.destination, self.opener)
        self.assertEqual(binary.read_bytes(), self.content)
        self.assertFalse((self.destination.parent / "unwanted").exists())
        self.assertEqual(list(self.destination.iterdir()), [binary])
        cached = fetch_core.acquire(
            self.asset, self.destination,
            lambda *_args, **_kwargs: self.fail("Кэш не должен обращаться к сети"),
        )
        self.assertEqual(cached, binary)

    def test_archive_hash_mismatch(self):
        self.asset["archiveSha256"] = "0" * 64
        with self.assertRaises(SystemExit):
            fetch_core.acquire(self.asset, self.destination, self.opener)
        self.assertFalse((self.destination / "sing-box").exists())

    def test_binary_hash_mismatch(self):
        self.asset["binarySha256"] = "0" * 64
        with self.assertRaises(SystemExit):
            fetch_core.acquire(self.asset, self.destination, self.opener)
        self.assertFalse((self.destination / "sing-box").exists())

    def test_symlink_member_is_rejected(self):
        self.archive = self.make_archive(symlink=True)
        self.asset["archiveSha256"] = hashlib.sha256(self.archive).hexdigest()
        with self.assertRaises(SystemExit):
            fetch_core.acquire(self.asset, self.destination, self.opener)
        self.assertFalse((self.destination / "sing-box").exists())

    def test_unknown_existing_binary_is_preserved(self):
        self.destination.mkdir()
        binary = self.destination / "sing-box"
        binary.write_bytes(b"preexisting-file")
        with self.assertRaises(SystemExit):
            fetch_core.acquire(self.asset, self.destination, self.opener)
        self.assertEqual(binary.read_bytes(), b"preexisting-file")


if __name__ == "__main__":
    unittest.main()
