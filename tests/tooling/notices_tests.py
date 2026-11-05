"""Reject changed project or upstream licenses, stale pins, and omitted notices."""

import json
import shutil
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.checks.notices import (
    BUNDLED_LICENSES,
    EMBEDDED_MIT_FILES,
    JAPANESE_GLYPH_CREDITS,
    SEPARATOR,
    check,
    sections,
)

ROOT = Path(__file__).resolve().parents[2]


def main():
    with TemporaryDirectory(prefix='notice-test-') as directory:
        root = Path(directory).resolve()
        files = [
            'LICENSE',
            'ThirdPartyNotices.txt',
            'sources.lock.json',
            'tools/assets/nightreign-keys.json',
        ]
        files += [f'third_party/{name}/{filename}' for name, filename in BUNDLED_LICENSES.items()]
        files += [f'third_party/imgui/{filename}' for filename in EMBEDDED_MIT_FILES]
        files.append('third_party/imgui/imgui_draw.cpp')
        for name in files:
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        check(root)
        notice = root / 'ThirdPartyNotices.txt'
        original = notice.read_text(encoding='utf-8')
        project_license = root / 'LICENSE'
        original_project_license = project_license.read_text(encoding='utf-8')
        pin_path = root / 'sources.lock.json'
        original_pins = pin_path.read_text(encoding='utf-8')
        pins = json.loads(original_pins)

        def reject(path, replacement):
            before = path.read_text(encoding='utf-8')
            if before == replacement:
                raise AssertionError('License rejection test did not change its input')
            path.write_text(replacement, encoding='utf-8')
            try:
                try:
                    check(root)
                except ValueError:
                    return
                raise AssertionError('Invalid project license or third-party notices were accepted')
            finally:
                path.write_text(before, encoding='utf-8')

        reject(
            project_license,
            original_project_license.replace(
                'Permission is hereby granted', 'Permission was granted'
            ),
        )
        reject(project_license, original_project_license.replace('Copyright (c) 2026 demondo', ''))
        reject(project_license, sections(original)['minhook'][1] + '\n')
        # A source upgrade must invalidate the old attribution even when terms are unchanged.
        pins[0]['Commit'] = '0' * 40
        reject(pin_path, json.dumps(pins))
        reject(notice, original.replace('THE SOFTWARE IS PROVIDED', 'THE SOFTWARE WAS PROVIDED', 1))
        reject(
            notice, original.replace('THIS SOFTWARE IS PROVIDED', 'THIS SOFTWARE WAS PROVIDED', 1)
        )
        reject(notice, original.replace('SOFTWARE.\n', 'SOFTWARE.\nRedundant terms.\n', 1))
        for copyright_notice in (
            'Copyright (c) 2014-2025 Omar Cornut',
            'Copyright (c) 2004, 2005 Tristan Grimmer',
            'Copyright (c) 2017 Sean Barrett',
            'Copyright (c) 2022 Sergii Kudlai',
        ):
            reject(notice, original.replace(copyright_notice, ''))
        reject(
            notice,
            original.replace(
                "stb: ImGui's imstb_rectpack.h, imstb_textedit.h, imstb_truetype.h\n", ''
            ),
        )
        reject(notice, original.replace('Doublemoses:', 'Someone else:', 1))
        for credit in (
            'Andre.Formats archive public keys:',
            'https://github.com/vawser/Smithbox/blob/c63eae720c00ffa48abad9b77906d0ed4a9f81e7/src/Andre/Andre.Formats/Util/ArchiveKeys.cs',
            'https://github.com/vawser/Smithbox/blob/c63eae720c00ffa48abad9b77906d0ed4a9f81e7/src/Andre/SoulsFormats/SoulsFormats/Util/Keys.cs',
            'Nightreign-Relic-Editor by yk23311:',
            'https://github.com/yk23311/Nightreign-Relic-Editor/tree/211c1fb5974fe3893072a9fa447656219481f292',
            'nightreign-relic-checker by sganggs:',
            'https://github.com/sganggs/nightreign-relic-checker/blob/6fe8267682149568058649b5f25a1481ac06384e/windows/renderer/core.js',
        ):
            reject(notice, original.replace(credit, '', 1))
        for credit in JAPANESE_GLYPH_CREDITS:
            reject(notice, original.replace(credit, '', 1))
        reject(notice, original.replace('License: CC-BY-4.0', 'License: MIT', 1))
        reject(
            notice,
            original.replace(
                'Revision: f5befd2d29e66809cd1110a152e375a7f1981f06', 'Revision: ' + '0' * 40, 1
            ),
        )
        reject(notice, original.replace('c63eae720c00ffa48abad9b77906d0ed4a9f81e7', '0' * 40))
        reject(
            notice,
            original.replace(
                '/blob/c63eae720c00ffa48abad9b77906d0ed4a9f81e7/', '/blob/' + '0' * 40 + '/', 1
            ),
        )
        parts = original.split(SEPARATOR)
        reject(notice, original.replace(SEPARATOR + parts[1], ''))
        reject(notice, original + SEPARATOR + parts[1])
        reject(
            notice, original.replace('Copyright (c) 2016 Tino Reichardt. All rights reserved.', '')
        )
        # A shared text is valid only while all selected upstream MIT terms agree.
        for name in ('third_party/bcdec/LICENSE', 'third_party/imgui/imstb_truetype.h'):
            path = root / name
            reject(
                path,
                path.read_text(encoding='utf-8').replace(
                    'Permission is hereby granted', 'Permission was granted', 1
                ),
            )
        minhook = root / 'third_party/minhook/LICENSE.txt'
        reject(minhook, minhook.read_text(encoding='utf-8').replace('REGENTS', 'AUTHORS', 1))
        draw = root / 'third_party/imgui/imgui_draw.cpp'
        reject(draw, draw.read_text(encoding='utf-8').replace('CC BY 4.0', 'CC BY 3.0', 1))
    print(
        'PASS: changed licenses, stale pins, missing/duplicate notices, and omitted copyrights or research credits are rejected.'
    )


if __name__ == '__main__':
    main()
