"""Freeze a clean development package as a release ZIP and checksum."""

import argparse
import hashlib
import re
import shutil
import stat
from dataclasses import dataclass
from pathlib import Path
from zipfile import ZIP_DEFLATED, BadZipFile, ZipFile

from tools.build.output import move_new, safe_path, temporary_directory
from tools.process import ROOT, cli
from tools.release.manifest import PACKAGE_FILES


@dataclass(frozen=True)
class ReleaseArtifacts:
    package: Path
    archive: Path
    checksum: Path
    sha256: str


def verify_package(package, candidate):
    if (
        not safe_path(package).is_dir()
        or {path.name for path in package.iterdir()} != PACKAGE_FILES
    ):
        raise ValueError(f'The release package contains unexpected or missing files: {package}')
    for filename in sorted(PACKAGE_FILES):
        path = safe_path(package / filename)
        if not path.is_file() or path.read_bytes() != (candidate / filename).read_bytes():
            raise ValueError(f'The release package does not match the candidate: {path}')


def checked_archive_digest(archive, package):
    if not safe_path(archive).is_file():
        raise ValueError(f'The release ZIP must be a file: {archive}')
    try:
        with archive.open('rb') as stream:
            with ZipFile(stream) as zipped:
                entries = zipped.infolist()
                if (
                    len(entries) != len(PACKAGE_FILES)
                    or {entry.orig_filename for entry in entries} != PACKAGE_FILES
                ):
                    raise ValueError(f'The ZIP contains unexpected or missing files: {archive}')
                for entry in entries:
                    kind = stat.S_IFMT(entry.external_attr >> 16)
                    if kind not in (0, stat.S_IFREG) or entry.external_attr & 0x10:
                        raise ValueError(
                            f'The ZIP entry must be a file: {archive}: {entry.filename}'
                        )
                    if zipped.read(entry) != (package / entry.filename).read_bytes():
                        raise ValueError(f'The ZIP changed {entry.filename}: {archive}')
            stream.seek(0)
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    except BadZipFile as error:
        raise ValueError(f'Invalid release ZIP: {archive}: {error}') from error


def checksum_bytes(archive, digest):
    return f'{digest}  {archive.name}\n'.encode()


def verify_checksum(checksum, expected):
    if not safe_path(checksum).is_file() or checksum.read_bytes() != expected:
        raise ValueError(f'The release checksum does not match the ZIP: {checksum}')


def publish(dist, origin, destination, recover):
    try:
        move_new(dist, origin, destination)
    except FileExistsError:
        if not recover:
            raise
        return False
    return True


def package_release(package_directory, version, output_directory=ROOT / 'dist', *, recover=False):
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[a-z0-9]+(?:[.-][a-z0-9]+)*)?', version):
        raise ValueError('Invalid release version')
    if version.endswith('-dev'):
        raise ValueError('The -dev suffix is reserved for replaceable development output')
    source, dist = safe_path(package_directory), safe_path(output_directory)
    items = list(source.iterdir())
    if {path.name for path in items} != PACKAGE_FILES or any(
        not safe_path(path).is_file() or path.stat().st_size == 0 for path in items
    ):
        raise ValueError('Package only a clean build containing the expected release files')
    name = f'ern-relics-{version}'
    package, archive = dist / name, dist / f'{name}.zip'
    checksum = dist / f'{name}.zip.sha256'
    existing = set()
    # Observe the outputs in reverse publication order so an in-flight publish
    # cannot make the checksum visible before its ZIP is observed.
    for path in (checksum, archive, package):
        if safe_path(path).exists():
            if not recover:
                raise FileExistsError(
                    f'Release output already exists; it will not be replaced: {path}'
                )
            existing.add(path)
    with temporary_directory(dist, '.stage-') as stage:
        staged_package = stage / name
        staged_package.mkdir()
        for filename in sorted(PACKAGE_FILES):
            shutil.copy2(source / filename, staged_package / filename)
        staged_archive = stage / archive.name
        if package in existing:
            verify_package(package, staged_package)
        if archive in existing:
            digest = checked_archive_digest(archive, staged_package)
        else:
            with ZipFile(staged_archive, 'w', compression=ZIP_DEFLATED) as zipped:
                for filename in sorted(PACKAGE_FILES):
                    zipped.write(staged_package / filename, filename)
            digest = checked_archive_digest(staged_archive, staged_package)
        staged_checksum = stage / checksum.name
        expected_checksum = checksum_bytes(archive, digest)
        if checksum in existing:
            verify_checksum(checksum, expected_checksum)
        else:
            staged_checksum.write_bytes(expected_checksum)
        if package not in existing and not publish(dist, staged_package, package, recover):
            verify_package(package, staged_package)
        if archive not in existing and not publish(dist, staged_archive, archive, recover):
            # A concurrent run can publish the same files in a different ZIP.
            digest = checked_archive_digest(archive, package)
            expected_checksum = checksum_bytes(archive, digest)
            if checksum in existing:
                verify_checksum(checksum, expected_checksum)
            else:
                staged_checksum.write_bytes(expected_checksum)
        if checksum not in existing and not publish(dist, staged_checksum, checksum, recover):
            verify_checksum(checksum, expected_checksum)
    return ReleaseArtifacts(package, archive, checksum, digest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package-directory', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output-directory', type=Path, default=ROOT / 'dist')
    parser.add_argument(
        '--recover', action='store_true', help='Verify and complete existing output'
    )
    args = parser.parse_args()
    release = package_release(
        args.package_directory, args.version, args.output_directory, recover=args.recover
    )
    print(f'Release ZIP and checksum ready: {release.archive} and {release.checksum}')


if __name__ == '__main__':
    cli(main)
