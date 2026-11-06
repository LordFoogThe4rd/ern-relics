"""Check build ownership, dependency boundaries, imports, and document links."""

import argparse
import ast
import json
import re
import sys
import tomllib
from pathlib import Path


def check_build_graph(root):
    """Keep the small, explicit CMake graph aligned with the architectural layers."""
    dependencies = {
        'ern_relic_model': set(),
        'ern_game_memory': {'ern_relic_model'},
        'ern_game_data': {
            'ern_relic_model',
            'bcrypt',
            'crypt32',
            'xmllite',
            'shlwapi',
            'zstd_decoder',
        },
        'ern_application': {'ern_game_memory', 'ern_game_data'},
        'ern_ui': {'ern_game_data', 'imgui'},
        'ern_runtime': {'ern_application', 'ern_ui', 'minhook', 'version', 'dinput8', 'dxguid'},
        'ern_relics': {'ern_runtime'},
    }
    owners = {
        'relics': 'ern_relic_model',
        'game': 'ern_game_memory',
        'data': 'ern_game_data',
        'ui': 'ern_ui',
    }
    links = {target: set() for target in dependencies}
    declared = set()
    for command, body in re.findall(
        r'\b(add_library|target_sources|target_link_libraries)\s*\(([^)]*)\)',
        (root / 'CMakeLists.txt').read_text(encoding='utf-8'),
    ):
        target, *arguments = body.split()
        if target not in dependencies:
            continue
        if command == 'add_library':
            declared.add(target)
        if command == 'target_link_libraries':
            links[target].update(
                arg for arg in arguments if arg not in {'PUBLIC', 'PRIVATE', 'INTERFACE'}
            )
        else:
            for source in arguments:
                parts = source.split('/')
                if len(parts) >= 3 and parts[0] == 'src':
                    owner = owners.get(parts[1])
                    if owner is not None and owner != target:
                        raise ValueError(f'{source}: belongs to {owner}, found in {target}')
    if declared != dependencies.keys():
        raise ValueError('CMake must declare each owned architectural target')
    for target, expected in dependencies.items():
        if links[target] != expected:
            raise ValueError(
                f'{target}: unexpected build dependencies {sorted(links[target] ^ expected)}'
            )


def check_source_registration(root):
    sources = {
        path.relative_to(root).as_posix()
        for directory in ('src', 'tests')
        for path in (root / directory).rglob('*.cpp')
    }
    registered = []
    for name in ('CMakeLists.txt', 'tests/CMakeLists.txt'):
        cmake = root / name
        for source in re.findall(r'\b[\w-]+(?:/[\w-]+)+\.cpp\b', cmake.read_text(encoding='utf-8')):
            if cmake.parent == root and not source.startswith('src/'):
                continue
            registered.append((cmake.parent / source).relative_to(root).as_posix())
    if sources != set(registered) or len(registered) != len(set(registered)):
        raise ValueError(f'CMake source registration differs: {sorted(sources ^ set(registered))}')
    return len(sources)


def check_cpp_boundaries(root):
    headers = {
        path.name: path.relative_to(root / directory).as_posix()
        for directory in ('src', 'tests')
        for path in (root / directory).rglob('*.hpp')
    }
    allowed = {'relics': {'relics'}, 'game': {'game', 'relics'}, 'data': {'data', 'relics'}}
    readonly = {
        'app/editor_view.hpp',
        'app/editor_command.hpp',
        'app/inspection.hpp',
        'data/asset_types.hpp',
        'data/catalog_presentation.hpp',
        'data/preset_types.hpp',
    }
    for directory in ('src', 'tests'):
        for path in (root / directory).rglob('*'):
            if path.suffix not in ('.cpp', '.hpp'):
                continue
            relative = path.relative_to(root)
            parts = relative.parts
            contents = path.read_text(encoding='utf-8')
            for include in re.findall(r'^\s*#include "([^"]+)"', contents, re.M):
                expected = headers.get(Path(include).name)
                if expected is not None and include != expected:
                    raise ValueError(f'{relative}: use "{expected}", found "{include}"')
                if expected is None and include not in ('bcdec.h', 'game_keys.hpp'):
                    raise ValueError(f'{relative}: unresolved include "{include}"')
                if parts[0] == 'src' and include.startswith('support/'):
                    raise ValueError(
                        f'{relative}: production code depends on test support {include}'
                    )
                if parts[0] == 'src' and parts[1] in allowed and '/' in include:
                    if include.split('/', 1)[0] not in allowed[parts[1]]:
                        raise ValueError(f'{relative}: forbidden dependency on {include}')
                if parts[:2] == ('src', 'ui'):
                    if not include.startswith(('ui/', 'relics/')) and include not in readonly:
                        raise ValueError(f'{relative}: UI depends on service {include}')
            if parts[:2] == ('src', 'ui'):
                if re.search(r'\b(?:runtime|application)\s*\(', contents):
                    raise ValueError(f'{relative}: UI must receive views and return commands')
                if re.search(
                    r'#include\s*<(?:fstream|cstdio|Windows.h|windows.h)>|'
                    r'\b(?:CreateFileW|ReadFile|WriteFile|GetWindowsDirectoryW|AddFontFromFileTTF)\s*\(|'
                    r'\bstd::(?:ifstream|ofstream|fstream|fopen|fread|fwrite)\b',
                    contents,
                ):
                    raise ValueError(f'{relative}: file I/O belongs on the application worker')


def check_python_imports(root):
    for path in (root / 'tools').rglob('*.py'):
        tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
        for node in ast.walk(tree):
            modules = []
            if isinstance(node, ast.Import):
                modules = [alias.name for alias in node.names]
            elif isinstance(node, ast.ImportFrom) and node.module:
                module_file = root.joinpath(*node.module.split('.')).with_suffix('.py')
                modules = (
                    [node.module]
                    if module_file.is_file()
                    else [node.module + '.' + alias.name for alias in node.names]
                )
            for module in modules:
                if not module.startswith('tools.'):
                    continue
                target = root.joinpath(*module.split('.'))
                if (
                    not target.with_suffix('.py').is_file()
                    and not (target / '__init__.py').is_file()
                ):
                    raise ValueError(f'{path.relative_to(root)}: unresolved import {module}')


def check_markdown_links(root):
    for path in [*root.glob('*.md'), *(root / 'docs').rglob('*.md')]:
        for target in re.findall(r'\[[^\]]*\]\(([^)]+)\)', path.read_text(encoding='utf-8')):
            target = target.strip('<>').split('#', 1)[0]
            if target and not re.match(r'^[a-z]+:', target) and not (path.parent / target).exists():
                raise ValueError(f'{path.relative_to(root)}: broken link {target}')


def check_dependency_pins(root):
    dependencies = json.loads((root / 'sources.lock.json').read_text(encoding='utf-8'))
    if len({entry['Name'] for entry in dependencies}) != len(dependencies):
        raise ValueError('Duplicate source dependency names')
    for entry in dependencies:
        if not re.fullmatch(r'[a-z][a-z0-9-]*', entry['Name']) or not re.fullmatch(
            r'[0-9a-f]{40}', entry['Commit']
        ):
            raise ValueError(f'Invalid source dependency: {entry}')
    pinned = {entry['Name'] for entry in dependencies}
    cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    used = set(re.findall(r'\bthird_party/([a-z][a-z0-9-]*)', cmake))
    if pinned != used:
        raise ValueError(
            f'Native dependency declarations differ: unused {sorted(pinned - used)}, unpinned {sorted(used - pinned)}'
        )


def check_python_dependencies(root):
    """Match direct packages to Python imports and the scripts' uv tool commands."""
    project = tomllib.loads((root / 'pyproject.toml').read_text(encoding='utf-8'))

    def package_name(name):
        return re.sub(r'[-_.]+', '-', re.match(r'[\w.-]+', name)[0]).lower()

    declared = {
        package_name(dependency)
        for group in project.get('dependency-groups', {}).values()
        for dependency in group
    }
    declared.update(
        package_name(dependency)
        for dependency in project.get('project', {}).get('dependencies', [])
    )
    used = set()
    for directory in ('tools', 'tests'):
        for path in (root / directory).rglob('*.py'):
            tree = ast.parse(path.read_text(encoding='utf-8'), filename=str(path))
            for node in ast.walk(tree):
                modules = []
                if isinstance(node, ast.Import):
                    modules = [alias.name for alias in node.names]
                elif isinstance(node, ast.ImportFrom) and node.level == 0 and node.module:
                    modules = [node.module]
                for module in modules:
                    name = module.split('.', 1)[0]
                    if name not in sys.stdlib_module_names and name not in {'tools', 'tests'}:
                        used.add(package_name({'PIL': 'pillow'}.get(name, name)))
        for path in (root / directory).rglob('*.ps1'):
            # The wrappers invoke packaged executables directly after --locked.
            commands = re.findall(
                r'(?m)^\s*& uv run [^\r\n]*?--locked\s+([a-z][\w-]*)',
                path.read_text(encoding='utf-8'),
            )
            used.update(package_name(command) for command in commands if command != 'python')
    if declared != used:
        raise ValueError(
            f'Python dependency declarations differ: unused {sorted(declared - used)}, undeclared {sorted(used - declared)}'
        )


def check_release_versions(root):
    """Keep the release manifest and simple strategy aligned with the native build."""
    manifest = json.loads((root / '.release-please-manifest.json').read_text(encoding='utf-8'))
    version = (root / 'version.txt').read_text(encoding='utf-8').strip()
    cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
    match = re.search(
        r'^project\(ern_relics VERSION (\d+\.\d+\.\d+) LANGUAGES C CXX\)'
        r' # x-release-please-version$',
        cmake,
        re.M,
    )
    if not match or manifest != {'.': version} or match[1] != version:
        raise ValueError('Release manifest, version.txt, and annotated CMake version must match')


def check(root):
    check_build_graph(root)
    source_count = check_source_registration(root)
    check_cpp_boundaries(root)
    check_python_imports(root)
    check_markdown_links(root)
    check_dependency_pins(root)
    check_python_dependencies(root)
    check_release_versions(root)
    print(
        f'PASS: {source_count} C++ sources registered; target graph, UI boundaries, imports, links, pins, dependency usage, and release versions match.'
    )


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    check(parser.parse_args().root.resolve())
