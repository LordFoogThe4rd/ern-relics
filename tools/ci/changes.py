"""Select native CI checks from the complete push or pull-request diff."""

import json
import os
import subprocess
from pathlib import Path, PurePosixPath


def needs_native(paths):
    # Only known documentation paths may skip Windows. New file types run checks.
    return any(
        not (path.suffix == '.md' and (len(path.parts) == 1 or path.parts[0] == 'docs'))
        for path in map(PurePosixPath, paths)
    )


def select_native(root, event_name, event):
    if event_name == 'push':
        base, head = event['before'], event['after']
        comparison = '..'
    elif event_name == 'pull_request':
        base = event['pull_request']['base']['sha']
        head = event['pull_request']['head']['sha']
        comparison = '...'
    else:
        return True
    if base == '0' * 40:
        return True
    try:
        # Disable rename detection so a source moved into docs still counts as code.
        result = subprocess.run(
            ['git', 'diff', '--name-only', '--no-renames', '-z', f'{base}{comparison}{head}', '--'],
            cwd=root,
            capture_output=True,
            check=True,
        )
    except subprocess.CalledProcessError:
        print('Comparison history unavailable; running native checks.')
        return True
    return needs_native(result.stdout.decode('utf-8').split('\0')[:-1])


def main():
    event = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text(encoding='utf-8'))
    root = Path(__file__).resolve().parents[2]
    native = select_native(root, os.environ['GITHUB_EVENT_NAME'], event)
    with Path(os.environ['GITHUB_OUTPUT']).open('a', encoding='utf-8') as output:
        output.write(f'native={str(native).lower()}\n')
    print(
        'Running Windows build and tests.'
        if native
        else 'Documentation-only: quick checks suffice.'
    )


if __name__ == '__main__':
    main()
