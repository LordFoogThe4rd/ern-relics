"""Verify the project license, MIT and BSD notices, CC BY attribution, and research credits."""

import argparse
import hashlib
import json
import re
from pathlib import Path

SEPARATOR = '\n' + '-' * 80 + '\n\n'
LICENSE_MARKER = '\nLicense text:\n\n'
ATTRIBUTION_MARKER = '\nAttribution:\n\n'
BUNDLED_LICENSES = {
    'imgui': 'LICENSE.txt',
    'minhook': 'LICENSE.txt',
    'zstd': 'LICENSE',
    'bcdec': 'LICENSE',
}
PROJECT_LICENSE_HASH = 'f222689b98a176b0fe2c36adb795b8aef7f0de1f8c43548863c6992a780520dd'
EMBEDDED_MIT_FILES = ('imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h')
# ProggyClean has no standalone license file in the ImGui build inputs.
PROGGYCLEAN_COPYRIGHT = 'Copyright (c) 2004, 2005 Tristan Grimmer'
JAPANESE_GLYPH_CREDITS = (
    'List of Joyo Kanji by the Agency for Cultural Affairs',
    'https://www.bunka.go.jp/kokugo_nihongo/sisaku/joho/joho/kijun/naikaku/kanji/',
    'List of Jinmeiyo Kanji by the Ministry of Justice',
    'http://www.moj.go.jp/MINJI/minji86.html',
    'Creative Commons Attribution 4.0 International (CC BY 4.0)',
    'https://creativecommons.org/licenses/by/4.0/legalcode',
    'https://github.com/vaiorabbit/everyday_use_kanji',
    'U+20B9F',
)


def mit_parts(text):
    """Read the selected MIT option, excluding any alternative license that follows."""
    header, marker, terms = text.partition('Permission is hereby granted')
    body, end, _ = terms.partition('SOFTWARE.')
    copyrights = re.findall(r'^Copyright .+$', header, re.M)
    if not marker or not end or not copyrights:
        raise ValueError('Missing MIT copyright or terms')
    return copyrights, ' '.join((marker + body + end).split())


def minhook_notice(text):
    """Share the identical HDE 32/64 notices without changing their terms."""
    prefix, marker, rest = text.partition('Hacker Disassembler Engine 32 C\n')
    hde32, separator, hde64 = rest.partition(
        '\n' + '-' * 79 + '\nHacker Disassembler Engine 64 C\n'
    )
    if not marker or not separator or hde32.strip() != hde64.strip():
        raise ValueError('Review changed MinHook HDE notices before consolidating them')
    return prefix + 'Hacker Disassembler Engine 32 C / 64 C\n' + hde64.strip()


def sections(text):
    result = {}
    for section in text.split(SEPARATOR)[1:]:
        header, marker, license_text = section.partition(LICENSE_MARKER)
        if not marker:
            header, marker, license_text = section.partition(ATTRIBUTION_MARKER)
        fields = dict(re.findall(r'^([A-Za-z ]+): (.+)$', header, re.M))
        name = fields.get('Component')
        if not marker or not name or not license_text.strip() or name in result:
            raise ValueError('Malformed, empty, or duplicate component notice')
        result[name] = (fields, license_text.strip())
    return result


def check(root):
    project_license = (root / 'LICENSE').read_text(encoding='utf-8').strip()
    if hashlib.sha256(project_license.encode('utf-8')).hexdigest() != PROJECT_LICENSE_HASH:
        raise ValueError('Project MIT license differs from the reviewed text')
    text = (root / 'ThirdPartyNotices.txt').read_text(encoding='utf-8')
    notices = sections(text)
    expected = {'mit', 'imgui-japanese', 'minhook', 'zstd'}
    if notices.keys() != expected:
        raise ValueError(f'Component notices differ: {sorted(notices.keys() ^ expected)}')
    pins = {
        entry['Name']: entry
        for entry in json.loads((root / 'sources.lock.json').read_text(encoding='utf-8'))
    }
    if pins.keys() != BUNDLED_LICENSES.keys():
        raise ValueError('Update notice coverage for changed source dependencies')
    for name in ('minhook', 'zstd'):
        fields, body = notices[name]
        if (
            fields.get('Revision') != pins[name]['Commit']
            or fields.get('Source') != pins[name]['Url']
        ):
            raise ValueError(f'{name}: notice does not match sources.lock.json')
        original = (
            (root / 'third_party' / name / BUNDLED_LICENSES[name])
            .read_text(encoding='utf-8')
            .strip()
        )
        if name == 'minhook':
            original = minhook_notice(original)
        if body != original:
            raise ValueError(f'{name}: full upstream license text differs')

    fields, body = notices['mit']
    if (
        fields.keys() != {'Component', 'License', 'imgui', 'proggyclean', 'stb', 'bcdec'}
        or fields['License'] != 'MIT'
    ):
        raise ValueError('Shared MIT notice must cover ImGui, ProggyClean, stb, and bcdec')
    _, terms = mit_parts(project_license)
    copyrights = []
    for name in ('imgui', 'bcdec'):
        if fields[name] != f'{pins[name]["Url"]} ({pins[name]["Commit"]})':
            raise ValueError(f'{name}: notice does not match sources.lock.json')
        original = (root / 'third_party' / name / BUNDLED_LICENSES[name]).read_text(
            encoding='utf-8'
        )
        upstream_copyrights, upstream_terms = mit_parts(original)
        if upstream_terms != terms:
            raise ValueError(f'{name}: upstream MIT terms differ from the shared notice')
        copyrights += upstream_copyrights
    for filename in EMBEDDED_MIT_FILES:
        original = (root / 'third_party/imgui' / filename).read_text(encoding='utf-8')
        stb_copyrights, stb_terms = mit_parts(original)
        if stb_terms != terms:
            raise ValueError(f'{filename}: upstream MIT terms differ from the shared notice')
        copyrights += stb_copyrights
    copyrights.append(PROGGYCLEAN_COPYRIGHT)
    actual_copyrights, actual_terms = mit_parts(body)
    if (
        sorted(actual_copyrights) != sorted(set(copyrights))
        or actual_terms != terms
        or ' '.join(body.split()) != ' '.join([*actual_copyrights, terms])
    ):
        raise ValueError('Shared MIT copyright notices or terms differ')

    fields, body = notices['imgui-japanese']
    revision = pins['imgui']['Commit']
    if fields != {
        'Component': 'imgui-japanese',
        'Source': f'https://github.com/ocornut/imgui/blob/{revision}/imgui_draw.cpp',
        'Revision': revision,
        'License': 'CC-BY-4.0',
    }:
        raise ValueError('Japanese glyph attribution does not match the pinned ImGui source')
    draw = (root / 'third_party/imgui/imgui_draw.cpp').read_text(encoding='utf-8')
    _, marker, japanese = draw.partition('ImFontAtlas::GetGlyphRangesJapanese()')
    comments, table, _ = japanese.partition('static const short')
    if not marker or not table:
        raise ValueError('Review the changed ImGui Japanese glyph table')
    for credit in JAPANESE_GLYPH_CREDITS:
        if credit not in comments or credit not in body:
            raise ValueError(f'Missing or changed Japanese glyph attribution: {credit}')

    credits = text.split(SEPARATOR)[0]
    keys = json.loads((root / 'tools/assets/nightreign-keys.json').read_text(encoding='utf-8'))
    revision = re.search(r'\b[0-9a-f]{40}\b', keys['source'])
    if not revision or f'https://github.com/vawser/Smithbox/tree/{revision[0]}' not in credits:
        raise ValueError('Format-key attributions do not match the Smithbox source revision')
    for path in (
        'src/Andre/Andre.Formats/Util/ArchiveKeys.cs',
        'src/Andre/SoulsFormats/SoulsFormats/Util/Keys.cs',
    ):
        if f'https://github.com/vawser/Smithbox/blob/{revision[0]}/{path}' not in credits:
            raise ValueError(f'Missing or stale format-key source: {path}')
    for credit in (
        'by Doublemoses:',
        'by Vawser, Katalash, Meowmaritus;',
        'SoulsFormats by TKGP, JKAnderson:',
        'Andre.Formats archive public keys:',
        'https://github.com/Doublemoses/NightreignRelicEditor/tree/2f747dcf78c7d41b5ba6b87f6e969688be4d62fc',
        'Nightreign-Relic-Editor by yk23311:',
        'https://github.com/yk23311/Nightreign-Relic-Editor/tree/211c1fb5974fe3893072a9fa447656219481f292',
        'nightreign-relic-checker by sganggs:',
        'https://github.com/sganggs/nightreign-relic-checker/blob/6fe8267682149568058649b5f25a1481ac06384e/windows/renderer/core.js',
        'https://github.com/mrtanger14-debug/RConnectMod/blob/fe5aac62cc69f40ed5e1e8ee81f2a87d86a28abd/RConnectMod/core/definitions/NightreignProgramData.cpp',
    ):
        if credit not in credits:
            raise ValueError(f'Missing research credit: {credit}')
    for copyright_notice in (
        'Copyright (c) Yann Collet - Meta Platforms, Inc',
        'Copyright (c) 2016 Tino Reichardt. All rights reserved.',
    ):
        if copyright_notice not in text:
            raise ValueError(f'Missing bundled copyright notice: {copyright_notice}')
    print(
        'PASS: project MIT license, seven bundled component notices, and research credits match pinned sources.'
    )


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    check(parser.parse_args().root.resolve())
