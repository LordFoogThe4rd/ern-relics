"""Reject duplicate compilation in the configured CMake build."""

import argparse
import json
from collections import Counter
from pathlib import Path


def check(build):
    commands = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
    sources = Counter((Path(entry['directory']) / entry['file']).resolve() for entry in commands)
    duplicates = [str(path) for path, count in sources.items() if count > 1]
    if not sources or duplicates:
        raise ValueError(f'Expected each source to compile once; duplicates: {duplicates}')
    print(f'PASS: {len(sources)} sources each compile once, shared by DLL variants and tests.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    check(parser.parse_args().build_directory)
