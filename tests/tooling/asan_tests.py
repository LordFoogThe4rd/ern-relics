"""Require the instrumented canary to report an actual buffer overflow."""

import argparse
import os
import subprocess
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('probe', type=Path)
    probe = parser.parse_args().probe.resolve()
    environment = dict(os.environ, ASAN_OPTIONS='halt_on_error=1:exitcode=1')
    result = subprocess.run(
        [str(probe)],
        cwd=probe.parent,
        env=environment,
        capture_output=True,
        text=True,
        errors='replace',
        timeout=15,
        creationflags=subprocess.CREATE_NO_WINDOW,
    )
    output = result.stdout + result.stderr
    if result.returncode == 0 or 'ERROR: AddressSanitizer: heap-buffer-overflow' not in output:
        raise AssertionError(
            f'ASan did not detect the canary overflow: exit {result.returncode}\n{output}'
        )
    print('PASS: AddressSanitizer detected the deliberate heap buffer overflow.')


if __name__ == '__main__':
    main()
