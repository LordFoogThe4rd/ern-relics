"""Exercise CI selection with real Git history, including renames and PR bases."""

import subprocess
import unittest
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.ci.changes import needs_native, select_native


class ChangeSelectionTests(unittest.TestCase):
    def test_only_known_documentation_skips_native(self):
        self.assertFalse(needs_native(['README.md', 'AGENTS.md', 'docs/development.md']))
        for path in (
            'src/app/runtime.cpp',
            'src/app/runtime.md',
            'config/ern-relics.ini',
            'tools/check.ps1',
            'tests/tooling/ci_changes_tests.py',
            '.github/workflows/ci.yml',
            '.github/actions/setup-native/action.yml',
            'CMakeLists.txt',
            'sources.lock.json',
            'uv.lock',
            'ThirdPartyNotices.txt',
            'new-file',
        ):
            with self.subTest(path=path):
                self.assertTrue(needs_native(['README.md', path]))

    def test_real_push_and_pull_request_history(self):
        with TemporaryDirectory(prefix='ci-changes-') as directory:
            root = Path(directory)

            def git(*args):
                return subprocess.run(
                    [
                        'git',
                        '-c',
                        'user.name=Fixture',
                        '-c',
                        'user.email=fixture@example.invalid',
                        '-c',
                        'commit.gpgsign=false',
                        *args,
                    ],
                    cwd=root,
                    capture_output=True,
                    text=True,
                    check=True,
                ).stdout.strip()

            def commit(name, text):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text, encoding='utf-8')
                git('add', '--all')
                git('commit', '--quiet', '-m', 'test: change fixture')
                return git('rev-parse', 'HEAD')

            def push(base, head):
                return select_native(root, 'push', {'before': base, 'after': head})

            git('init', '--quiet', '--template=')
            initial = commit('README.md', 'Initial')
            docs = commit('docs/development.md', 'Documentation')
            self.assertFalse(push(initial, docs))
            code = commit('src/model.cpp', '// Source')
            latest = commit('README.md', 'Latest docs')
            self.assertTrue(push(docs, latest), 'A multi-commit push must include earlier code.')
            self.assertFalse(push(code, latest))

            git('mv', 'src/model.cpp', 'docs/model.md')
            git('commit', '--quiet', '-m', 'test: rename source to docs')
            self.assertTrue(push(latest, git('rev-parse', 'HEAD')))

            # The PR adds only docs while main advances independently with code.
            git('checkout', '--quiet', '--detach', initial)
            advanced_base = commit('src/main.cpp', '// Main advanced')
            pr = {'pull_request': {'base': {'sha': advanced_base}, 'head': {'sha': docs}}}
            self.assertFalse(select_native(root, 'pull_request', pr))
            pr['pull_request']['head']['sha'] = code
            self.assertTrue(select_native(root, 'pull_request', pr))

            self.assertTrue(push('0' * 40, docs))
            self.assertTrue(push('1' * 40, docs), 'Missing history must run native checks.')
            self.assertTrue(select_native(root, 'workflow_dispatch', {}))


if __name__ == '__main__':
    unittest.main()
