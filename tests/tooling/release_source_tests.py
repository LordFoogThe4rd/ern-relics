"""Reject dirty source, changed dependencies, replaced ZIPs, and mismatched provenance."""

import copy
import hashlib
import json
import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory
from zipfile import ZipFile

from tools.ci.release_source import PACKAGE_FILES, PREDICATE_TYPE, build_record, verify_record


class ReleaseSourceTests(unittest.TestCase):
    def setUp(self):
        self.temporary = TemporaryDirectory(prefix='release-source-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.source = self.root / 'source'
        self.tooling = self.root / 'tooling'
        for root in (self.source, self.tooling):
            root.mkdir()
            self.git(root, 'init', '--quiet', '--template=')
            self.git(root, 'remote', 'add', 'origin', 'https://github.com/owner/repository')
            (root / '.gitignore').write_text('/dist/\n/third_party/\n')
            (root / 'version.txt').write_text('1.0.1\n')
            (root / 'sources.lock.json').write_text('[]')
            self.git(root, 'add', '--all')
            self.git(root, 'commit', '--quiet', '-m', f'test: {root.name} fixture')
        self.source_commit = self.git(self.source, 'rev-parse', 'HEAD')
        self.tooling_commit = self.git(self.tooling, 'rev-parse', 'HEAD')
        self.artifacts = self.root / 'artifacts'
        self.artifacts.mkdir()
        self.package = self.source / 'dist/ern-relics-1.0.1-dev'
        self.package.mkdir(parents=True)
        for name in PACKAGE_FILES:
            (self.package / name).write_bytes(name.encode())
        self.archive = self.artifacts / 'ern-relics-1.0.1.zip'
        with ZipFile(self.archive, 'w') as zip_file:
            for name in PACKAGE_FILES:
                zip_file.write(self.package / name, name)

        digest = hashlib.sha256(self.archive.read_bytes()).hexdigest()
        self.checksum = self.artifacts / (self.archive.name + '.sha256')
        self.checksum.write_text(f'{digest}  {self.archive.name}\n')

    def git(self, root, *args):
        return subprocess.check_output(
            [
                'git',
                '-C',
                str(root),
                '-c',
                'user.name=Fixture',
                '-c',
                'user.email=fixture@example.invalid',
                '-c',
                'commit.gpgsign=false',
                '-c',
                'core.hooksPath=',
                *args,
            ],
            text=True,
        ).strip()

    def record(self, **changes):
        arguments = dict(
            source=self.source,
            tooling=self.tooling,
            repository='owner/repository',
            tag='v1.0.1',
            source_commit=self.source_commit,
            tooling_commit=self.tooling_commit,
            artifacts=self.artifacts,
            run='https://github.com/owner/repository/actions/runs/123',
        )
        return build_record(**(arguments | changes))

    def test_clean_tagged_package_and_signed_claim(self):
        record = self.record()
        self.assertEqual(record['sourceCommit'], self.source_commit)
        statement = {
            'predicateType': PREDICATE_TYPE,
            'predicate': record,
            'subject': [{'digest': {'sha256': record['archive']['sha256']}}],
        }
        results = [{'verificationResult': {'statement': statement}}]
        verify_record(record, results)
        wrong = copy.deepcopy(results)
        wrong[0]['verificationResult']['statement']['predicate']['sourceCommit'] = 'f' * 40
        with self.assertRaises(ValueError):
            verify_record(record, wrong)
        wrong = copy.deepcopy(results)
        wrong[0]['verificationResult']['statement']['subject'][0]['digest']['sha256'] = 'f' * 64
        with self.assertRaises(ValueError):
            verify_record(record, wrong)

    def test_reject_wrong_commits_tag_and_dirty_source(self):
        for changes in (
            {'source_commit': 'f' * 40},
            {'source_commit': self.tooling_commit},
            {'tooling_commit': self.source_commit},
            {'tooling_commit': 'f' * 40},
            {'tag': 'main'},
            {'tag': 'v1.0.2'},
            {'repository': '../repository'},
        ):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                self.record(**changes)
        for root in (self.source, self.tooling):
            (root / 'version.txt').write_text('1.0.2')
            with self.assertRaises(ValueError):
                self.record()
            self.git(root, 'checkout', '--', 'version.txt')

    def test_reject_changed_or_redirected_dependencies(self):
        dependency = self.source / 'third_party/fixture'
        dependency.mkdir(parents=True)
        self.git(dependency, 'init', '--quiet', '--template=')
        self.git(dependency, 'remote', 'add', 'origin', 'https://github.com/upstream/fixture')
        (dependency / 'fixture.txt').write_text('locked bytes')
        self.git(dependency, 'add', '--all')
        self.git(dependency, 'commit', '--quiet', '-m', 'test: dependency fixture')
        pin = {
            'Name': 'fixture',
            'Url': 'https://github.com/upstream/fixture',
            'Commit': self.git(dependency, 'rev-parse', 'HEAD'),
        }
        (self.source / 'sources.lock.json').write_text(json.dumps([pin]))
        self.git(self.source, 'add', 'sources.lock.json')
        self.git(self.source, 'commit', '--quiet', '-m', 'test: lock dependency')
        self.source_commit = self.git(self.source, 'rev-parse', 'HEAD')
        self.assertEqual(self.record()['dependencies'], [pin])
        (dependency / 'fixture.txt').write_text('changed bytes')
        with self.assertRaises(ValueError):
            self.record()
        self.git(dependency, 'checkout', '--', 'fixture.txt')
        self.git(dependency, 'remote', 'set-url', 'origin', 'https://github.com/other/fixture')
        with self.assertRaises(ValueError):
            self.record()

    def test_reject_replaced_production_bytes_or_checksum(self):
        (self.package / 'ern-relics.dll').write_bytes(b'replaced')
        with self.assertRaises(ValueError):
            self.record()
        (self.package / 'ern-relics.dll').write_bytes(b'ern-relics.dll')
        self.checksum.write_text('0' * 64 + '  ern-relics-1.0.1.zip\n')
        with self.assertRaises(ValueError):
            self.record()


if __name__ == '__main__':
    unittest.main()
