"""Verify release bytes and checksums and reject contaminated or existing output."""

import hashlib
import shutil
import unittest
from itertools import combinations
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo

from tools.build.output import move_new, temporary_directory
from tools.process import ROOT
from tools.release.manifest import PACKAGE_FILES
from tools.release.package import package_release


class PackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory(prefix='ern-package-tests-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source [package]'
        self.source.mkdir()
        for filename in PACKAGE_FILES:
            (self.source / filename).write_bytes(filename.encode())

    def test_package_bytes_checksum_and_existing_output(self):
        # Stage on the output volume even when hosted-runner TEMP is on another drive.
        with temporary_directory(ROOT / 'output', 'package-test-') as output:
            result = package_release(self.source, '1.0.3', output)
            with ZipFile(result.archive) as zipped:
                self.assertEqual(set(zipped.namelist()), PACKAGE_FILES)
                for filename in PACKAGE_FILES:
                    self.assertEqual(zipped.read(filename), (self.source / filename).read_bytes())
            digest = hashlib.sha256(result.archive.read_bytes()).hexdigest()
            self.assertEqual(
                result.checksum.read_bytes(), f'{digest}  {result.archive.name}\n'.encode()
            )
            original = result.archive.read_bytes()
            with self.assertRaises(FileExistsError):
                package_release(self.source, '1.0.3', output)
            self.assertEqual(result.archive.read_bytes(), original)
            self.assertFalse(list(output.glob('.stage-*')))

    def test_every_existing_release_artifact_is_preserved(self):
        for suffix in ('', '.zip', '.zip.sha256'):
            with self.subTest(suffix=suffix):
                output = self.root / f'output-{len(suffix)}'
                output.mkdir()
                frozen = output / f'ern-relics-1.0.3{suffix}'
                frozen.write_bytes(b'preserve')
                with self.assertRaises(FileExistsError):
                    package_release(self.source, '1.0.3', output)
                self.assertEqual(frozen.read_bytes(), b'preserve')

    def test_recovery_publishes_only_missing_artifacts(self):
        original = package_release(self.source, '1.0.3', self.root / 'original')
        paths = (original.package, original.archive, original.checksum)
        for count in range(len(paths) + 1):
            for present in combinations(paths, count):
                with self.subTest(present=present):
                    output = self.root / 'output'
                    output.mkdir()
                    try:
                        for path in present:
                            copy = shutil.copytree if path.is_dir() else shutil.copy2
                            copy(path, output / path.name)
                        with patch('tools.release.package.move_new', wraps=move_new) as moved:
                            result = package_release(self.source, '1.0.3', output, recover=True)
                        self.assertEqual(result.sha256, original.sha256)
                        self.assertEqual(
                            result.checksum.read_bytes(), original.checksum.read_bytes()
                        )
                        self.assertEqual(
                            [call.args[2].name for call in moved.call_args_list],
                            [path.name for path in paths if path not in present],
                        )
                        self.assertEqual(
                            set(output.iterdir()), {output / path.name for path in paths}
                        )
                    finally:
                        shutil.rmtree(output)

    def test_recovery_rejects_conflicting_existing_artifacts_before_publishing(self):
        for conflict in ('package', 'archive', 'duplicate', 'invalid', 'checksum'):
            with self.subTest(conflict=conflict):
                output = self.root / conflict
                output.mkdir()
                package = output / 'ern-relics-1.0.3'
                archive = output / 'ern-relics-1.0.3.zip'
                checksum = output / 'ern-relics-1.0.3.zip.sha256'
                if conflict == 'package':
                    shutil.copytree(self.source, package)
                    (package / 'LICENSE').write_bytes(b'different package')
                    existing, error = package, 'package does not match'
                elif conflict in ('archive', 'duplicate'):
                    with ZipFile(archive, 'w') as zipped:
                        for filename in sorted(PACKAGE_FILES):
                            content = (self.source / filename).read_bytes()
                            if filename == 'LICENSE' and conflict == 'archive':
                                content = b'different ZIP contents'
                            zipped.writestr(filename, content)
                        if conflict == 'duplicate':
                            with self.assertWarnsRegex(UserWarning, 'Duplicate name'):
                                zipped.writestr('LICENSE', b'LICENSE')
                    existing = archive
                    error = 'ZIP changed' if conflict == 'archive' else 'unexpected or missing'
                elif conflict == 'invalid':
                    archive.write_bytes(b'invalid ZIP')
                    existing, error = archive, 'Invalid release ZIP'
                else:
                    checksum.write_bytes(b'incorrect checksum')
                    existing, error = checksum, 'checksum does not match'
                with patch('tools.release.package.move_new') as moved:
                    with self.assertRaisesRegex(ValueError, error):
                        package_release(self.source, '1.0.3', output, recover=True)
                    moved.assert_not_called()
                self.assertEqual(set(output.iterdir()), {existing})

    def test_recovery_uses_the_bytes_of_a_concurrently_published_zip(self):
        output = self.root / 'output'
        archive = output / 'ern-relics-1.0.3.zip'
        competing_archive = self.root / 'other.zip'
        with ZipFile(competing_archive, 'w') as zipped:
            for filename in sorted(PACKAGE_FILES):
                zipped.writestr(ZipInfo(filename), (self.source / filename).read_bytes())
        competing_bytes = competing_archive.read_bytes()

        def publish_first(dist, origin, destination):
            if destination == archive:
                self.assertNotEqual(origin.read_bytes(), competing_bytes)
                competing_archive.rename(destination)
            move_new(dist, origin, destination)

        with patch('tools.release.package.move_new', side_effect=publish_first):
            result = package_release(self.source, '1.0.3', output, recover=True)
        digest = hashlib.sha256(competing_bytes).hexdigest()
        self.assertEqual(result.archive.read_bytes(), competing_bytes)
        self.assertEqual(result.sha256, digest)
        self.assertEqual(result.checksum.read_bytes(), f'{digest}  {archive.name}\n'.encode())
        self.assertEqual(package_release(self.source, '1.0.3', output, recover=True), result)

    def test_recovery_rejects_a_concurrently_published_package_from_another_build(self):
        output = self.root / 'output'
        package = output / 'ern-relics-1.0.3'

        def publish_first(dist, origin, destination):
            self.assertEqual(destination, package)
            shutil.copytree(self.source, destination)
            (destination / 'LICENSE').write_bytes(b'different package')
            move_new(dist, origin, destination)

        with patch('tools.release.package.move_new', side_effect=publish_first):
            with self.assertRaisesRegex(ValueError, 'package does not match'):
                package_release(self.source, '1.0.3', output, recover=True)
        self.assertEqual(set(output.iterdir()), {package})

    def test_reject_runtime_files_directories_missing_notices_and_empty_files(self):
        for name in ('journal.bin', 'presets.json', 'unexpected.ini'):
            path = self.source / name
            path.write_bytes(b'local state')
            with self.assertRaises(ValueError):
                package_release(self.source, '1.0.3', self.root / 'out')
            path.unlink()
        directory = self.source / 'presets'
        directory.mkdir()
        with self.assertRaises(ValueError):
            package_release(self.source, '1.0.3', self.root / 'out')
        directory.rmdir()
        (self.source / 'LICENSE').write_bytes(b'')
        with self.assertRaises(ValueError):
            package_release(self.source, '1.0.3', self.root / 'out')
        (self.source / 'LICENSE').unlink()
        with self.assertRaises(ValueError):
            package_release(self.source, '1.0.3', self.root / 'out')
        self.assertFalse((self.root / 'out').exists())


if __name__ == '__main__':
    unittest.main()
