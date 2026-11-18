"""A release freezes its private checked candidate despite development rebuilds."""

import unittest
from io import StringIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch
from zipfile import ZipFile

from tools.build.output import move_new, write_build_output
from tools.release.build import build_release
from tools.release.manifest import PROJECT_FILES
from tools.release.package import package_release


class ReleaseBuildTests(unittest.TestCase):
    def setUp(self):
        temporary = TemporaryDirectory(prefix='ern-release-build-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = self.root / 'dist'
        (self.root / 'version.txt').write_text('1.0.2\n')
        for name, source in PROJECT_FILES.items():
            path = self.root / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(name.encode())
        self.runtime = self.root / 'runtime'
        self.runtime.mkdir()
        self.dll = self.runtime / 'ern-relics.dll'
        self.dll.write_bytes(b'checked candidate')
        self.used = {}

    def check(self, *, build_directory, package_directory, jobs, root):
        self.assertTrue(build_directory.is_dir())
        self.assertNotEqual(build_directory, root / '.build/release')
        self.used.update(build=build_directory, candidate=package_directory)
        candidate = write_build_output(
            root, self.runtime, 'ern-relics-1.0.2-dev', destination=package_directory
        )
        # A development build publishes different bytes after this check.
        self.dll.write_bytes(b'unrelated development rebuild')
        write_build_output(root, self.runtime, 'ern-relics-1.0.2-dev')
        return candidate

    def test_candidate_is_private_until_freezing_and_owned_directories_are_cleaned(self):
        with patch('tools.release.build.check', side_effect=self.check):
            release = build_release(self.output, root=self.root)
        with ZipFile(release.archive) as archive:
            self.assertEqual(archive.read('ern-relics.dll'), b'checked candidate')
        self.assertEqual(
            (self.output / 'ern-relics-1.0.2-dev/ern-relics.dll').read_bytes(),
            b'unrelated development rebuild',
        )
        self.assertFalse(self.used['build'].exists())
        self.assertFalse(self.used['candidate'].parent.exists())

    def test_interrupted_publication_keeps_the_original_checked_candidate(self):
        def interrupt(dist, origin, destination):
            if destination.suffix == '.zip':
                raise OSError('interrupted release')
            move_new(dist, origin, destination)

        with (
            patch('tools.release.build.check', side_effect=self.check),
            patch('sys.stderr', new_callable=StringIO) as stderr,
        ):
            with patch('tools.release.package.move_new', side_effect=interrupt):
                with self.assertRaisesRegex(OSError, 'interrupted release'):
                    build_release(self.output, root=self.root)
            candidate = self.used['candidate']
            self.assertIn(str(candidate), stderr.getvalue())
            self.assertEqual((candidate / self.dll.name).read_bytes(), b'checked candidate')
            self.assertFalse(self.used['build'].exists())
            with self.assertRaisesRegex(ValueError, 'package does not match'):
                build_release(self.output, root=self.root, recover=True)
            self.assertEqual(
                (self.used['candidate'] / self.dll.name).read_bytes(),
                b'unrelated development rebuild',
            )
        release = package_release(candidate, '1.0.2', self.output, recover=True)
        with ZipFile(release.archive) as archive:
            self.assertEqual(archive.read(self.dll.name), b'checked candidate')


if __name__ == '__main__':
    unittest.main()
