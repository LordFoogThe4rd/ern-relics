"""Record and verify the tagged source used by an attested release build."""

import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path
from zipfile import ZipFile

PREDICATE_TYPE = 'https://github.com/demondobf/ern-relics/attestations/release-source/v1'
PACKAGE_FILES = {
    'ern-relics.dll',
    'ern-relics.ini',
    'nightreign-relics.me3',
    'LICENSE',
    'ThirdPartyNotices.txt',
}


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def clean_checkout(root, commit):
    if not re.fullmatch(r'[0-9a-f]{40}', commit) or git(root, 'rev-parse', 'HEAD') != commit:
        raise ValueError(f'{root}: checkout does not match the expected commit')
    if git(root, 'status', '--porcelain', '--untracked-files=all'):
        raise ValueError(f'{root}: source checkout has changes')


def build_record(source, tooling, repository, tag, source_commit, tooling_commit, artifacts, run):
    match = re.fullmatch(r'v(\d+\.\d+\.\d+(?:-[a-z0-9]+(?:[.-][a-z0-9]+)*)?)', tag)
    if not match or not re.fullmatch(r'[\w.-]+/[\w.-]+', repository):
        raise ValueError('Invalid release tag or repository')
    version = match[1]
    clean_checkout(source, source_commit)
    clean_checkout(tooling, tooling_commit)
    expected_origin = f'https://github.com/{repository}'
    for root in (source, tooling):
        if git(root, 'remote', 'get-url', 'origin').removesuffix('.git') != expected_origin:
            raise ValueError(f'{root}: unexpected source repository')
    if (source / 'version.txt').read_text(encoding='utf-8').strip() != version:
        raise ValueError('Tagged source version does not match the release tag')
    dependencies = json.loads((source / 'sources.lock.json').read_text(encoding='utf-8'))
    for dependency in dependencies:
        root = source / 'third_party' / dependency['Name']
        clean_checkout(root, dependency['Commit'])
        if git(root, 'remote', 'get-url', 'origin') != dependency['Url']:
            raise ValueError(f'{root}: dependency origin differs from its lock')
    archive = artifacts / f'ern-relics-{version}.zip'
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    checksum = (artifacts / (archive.name + '.sha256')).read_text(encoding='utf-8')
    if checksum != f'{digest}  {archive.name}\n':
        raise ValueError('Release checksum does not match the ZIP')
    with ZipFile(archive) as zip_file:
        if (
            len(zip_file.infolist()) != len(PACKAGE_FILES)
            or set(zip_file.namelist()) != PACKAGE_FILES
        ):
            raise ValueError('Unexpected release ZIP contents')
        package = source / 'dist' / f'ern-relics-{version}-dev'
        for name in PACKAGE_FILES:
            if zip_file.read(name) != (package / name).read_bytes():
                raise ValueError(f'{name}: release differs from the checked production package')
    return {
        'schemaVersion': 1,
        'repository': expected_origin,
        'tag': tag,
        'sourceCommit': source_commit,
        'toolingCommit': tooling_commit,
        'workflowRun': run,
        'dependencies': dependencies,
        'archive': {'name': archive.name, 'sha256': digest},
    }


def verify_record(record, results):
    # The CLI verifies signatures, workflow identity, runner type, and subject digest first.
    for result in results:
        statement = result['verificationResult']['statement']
        if statement['predicateType'] != PREDICATE_TYPE or statement['predicate'] != record:
            continue
        if any(
            subject['digest'].get('sha256') == record['archive']['sha256']
            for subject in statement['subject']
        ):
            return
    raise ValueError('Verified attestation does not match the checked release source and ZIP')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    record = commands.add_parser('record')
    for name in ('source', 'tooling', 'artifacts'):
        record.add_argument('--' + name, type=Path, required=True)
    for name in ('tag', 'source-commit'):
        record.add_argument('--' + name, required=True)
    verify = commands.add_parser('verify')
    verify.add_argument('--record', type=Path, required=True)
    verify.add_argument('--verification', type=Path, required=True)
    args = parser.parse_args()
    if args.command == 'record':
        repository = os.environ['GITHUB_REPOSITORY']
        run = f'https://github.com/{repository}/actions/runs/{os.environ["GITHUB_RUN_ID"]}'
        result = build_record(
            args.source,
            args.tooling,
            repository,
            args.tag,
            args.source_commit,
            os.environ['GITHUB_SHA'],
            args.artifacts,
            run,
        )
        path = args.artifacts / f'ern-relics-{args.tag[1:]}.source.json'
        path.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
        print(f'Checked source provenance: {path}')
    else:
        verify_record(
            json.loads(args.record.read_text(encoding='utf-8')),
            json.loads(args.verification.read_text(encoding='utf-8-sig')),
        )
        print('Verified attestation matches the checked tagged source and release ZIP.')


if __name__ == '__main__':
    main()
