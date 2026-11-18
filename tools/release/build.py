"""Run the full check and create a new immutable release package."""

import argparse
import sys
from contextlib import nullcontext
from pathlib import Path
from tempfile import mkdtemp

from tools.build.output import remove_child, safe_path, temporary_directory
from tools.check import check
from tools.process import ROOT, cli
from tools.release.package import package_release


def build_release(
    output_directory=ROOT / 'dist', build_directory=None, jobs=0, *, root=ROOT, recover=False
):
    version = (root / 'version.txt').read_text(encoding='utf-8').strip()
    context = (
        temporary_directory(root / '.build', 'release-')
        if build_directory is None
        else nullcontext(build_directory)
    )
    dist = safe_path(root / 'dist')
    dist.mkdir(parents=True, exist_ok=True)
    staging = Path(mkdtemp(prefix='.release-', dir=dist))
    keep_candidate = False
    try:
        with context as native:
            package = check(
                build_directory=native,
                jobs=jobs,
                root=root,
                package_directory=staging / 'package',
            )
        keep_candidate = True
        release = package_release(package, version, output_directory, recover=recover)
        keep_candidate = False
        return release
    finally:
        if keep_candidate:
            print(f'Checked release candidate kept for recovery: {package}', file=sys.stderr)
        else:
            remove_child(dist, staging)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-directory', type=Path, default=ROOT / 'dist')
    parser.add_argument('--build-directory', type=Path)
    parser.add_argument('--jobs', type=int, default=0)
    parser.add_argument(
        '--recover', action='store_true', help='Verify and complete existing output'
    )
    args = parser.parse_args()
    release = build_release(
        args.output_directory, args.build_directory, args.jobs, recover=args.recover
    )
    print(f'Ready for Nexus Mods upload or ME3 import: {release.archive}')


if __name__ == '__main__':
    cli(main)
