"""Prove that build and include boundaries reject architectural regressions."""

import json
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.checks.layout import check


def main():
    with TemporaryDirectory(prefix='layout-test-') as directory:
        root = Path(directory)
        files = {
            'CMakeLists.txt': """
project(ern_relics VERSION 1.0.0 LANGUAGES C CXX) # x-release-please-version
add_library(ern_relic_model STATIC src/relics/model.cpp)
add_library(ern_game_memory STATIC src/game/memory.cpp)
target_link_libraries(ern_game_memory PUBLIC ern_relic_model)
add_library(ern_game_data STATIC src/data/source.cpp)
target_link_libraries(ern_game_data PUBLIC ern_relic_model PRIVATE bcrypt crypt32 xmllite shlwapi zstd_decoder)
add_library(ern_application STATIC src/app/controller.cpp)
target_link_libraries(ern_application PUBLIC ern_game_memory ern_game_data)
add_library(ern_ui STATIC src/ui/view.cpp)
target_link_libraries(ern_ui PUBLIC ern_game_data imgui)
add_library(ern_runtime OBJECT)
target_link_libraries(ern_runtime PUBLIC ern_application ern_ui minhook version dinput8 dxguid)
add_library(ern_relics SHARED)
target_link_libraries(ern_relics PRIVATE ern_runtime)
""",
            'tests/CMakeLists.txt': '',
            'sources.lock.json': '[]',
            '.release-please-manifest.json': '{".": "1.0.0"}',
            'version.txt': '1.0.0\n',
            'pyproject.toml': '[dependency-groups]\ndev = ["ruff==0.17.3"]\nassets = ["Pillow>=11.3,<13"]\n',
            'README.md': '',
            'tools/check.py': '',
            'tools/check.ps1': '& uv run --locked ruff check .',
            'tests/read.py': 'from PIL import Image',
            'src/relics/model.hpp': '',
            'src/relics/model.cpp': '#include "relics/model.hpp"',
            'src/data/source.hpp': '',
            'src/data/source.cpp': '',
            'src/data/preset_store.hpp': '',
            'src/game/memory.cpp': '#include "relics/model.hpp"',
            'src/ui/view.cpp': '',
            'src/app/bootstrap.hpp': '',
            'src/app/runtime.hpp': '',
            'src/app/editor_view.hpp': '',
            'src/app/controller.cpp': '',
            'tests/support/fixture.hpp': '',
        }
        for name, contents in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding='utf-8')
        check(root)
        for name, contents in (
            ('src/relics/model.cpp', '#include "data/source.hpp"'),
            ('src/game/memory.cpp', '#include "data/source.hpp"'),
            ('src/ui/view.cpp', 'auto &rt = runtime();'),
            ('src/ui/view.cpp', 'application().submit({});'),
            ('src/ui/view.cpp', '#include "app/runtime.hpp"'),
            ('src/ui/view.cpp', '#include "data/preset_store.hpp"'),
            ('src/ui/view.cpp', '#include <fstream>'),
            ('src/ui/view.cpp', 'atlas.AddFontFromFileTTF("font.ttf", 18);'),
            ('src/app/bootstrap.hpp', '#include "support/fixture.hpp"'),
            (
                'CMakeLists.txt',
                files['CMakeLists.txt']
                + '\ntarget_link_libraries(ern_relic_model PRIVATE ern_game_data)',
            ),
            (
                'CMakeLists.txt',
                files['CMakeLists.txt'].replace(
                    'PUBLIC ern_game_memory ern_game_data', 'PUBLIC ern_game_memory imgui'
                ),
            ),
            (
                'CMakeLists.txt',
                files['CMakeLists.txt']
                .replace('STATIC src/relics/model.cpp', 'STATIC')
                .replace(
                    'STATIC src/game/memory.cpp', 'STATIC src/game/memory.cpp src/relics/model.cpp'
                ),
            ),
            (
                'CMakeLists.txt',
                files['CMakeLists.txt']
                + '\ntarget_sources(ern_relic_model PRIVATE src/relics/model.cpp)',
            ),
            ('tools/check.py', 'import tools.missing'),
            ('README.md', '[Missing](docs/missing.md)'),
            ('sources.lock.json', json.dumps([{'Name': 'fixture', 'Commit': '0' * 40}] * 2)),
            ('sources.lock.json', json.dumps([{'Name': 'fixture', 'Commit': 'not-a-pin'}])),
            ('sources.lock.json', json.dumps([{'Name': 'unused', 'Commit': '0' * 40}])),
            ('CMakeLists.txt', files['CMakeLists.txt'] + '\nset(UNPINNED third_party/unpinned)'),
            (
                'pyproject.toml',
                files['pyproject.toml'].replace('"ruff==0.17.3"', '"ruff==0.17.3", "unused==1.0"'),
            ),
            ('pyproject.toml', files['pyproject.toml'].replace('"Pillow>=11.3,<13"', '')),
            ('tests/read.py', ''),
            ('tools/check.ps1', '& uv run --locked undeclared check .'),
            ('tools/check.py', 'import undeclared'),
            ('version.txt', '1.0.1\n'),
            ('.release-please-manifest.json', '{".": "1.0.1"}'),
            (
                'CMakeLists.txt',
                files['CMakeLists.txt'].replace('VERSION 1.0.0', 'VERSION 1.0.1'),
            ),
            (
                'CMakeLists.txt',
                files['CMakeLists.txt'].replace(' # x-release-please-version', ''),
            ),
        ):
            path = root / name
            path.write_text(contents, encoding='utf-8')
            try:
                check(root)
            except ValueError:
                pass
            else:
                raise AssertionError(f'Architecture violation accepted: {name}')
            finally:
                path.write_text(files[name], encoding='utf-8')
    print(
        'PASS: include and UI boundaries, source registration, target graph, imports, links, dependency pins, unused or undeclared dependencies, and release version drift are checked.'
    )


if __name__ == '__main__':
    main()
