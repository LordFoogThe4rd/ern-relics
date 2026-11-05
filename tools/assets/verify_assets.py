"""Compare local native name/icon decoding with an independent Python extraction.

Write audit evidence to the supplied output directory.
"""

import argparse
import io
import json
import math
import struct
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path
from tempfile import TemporaryDirectory

from PIL import Image

from tools.assets import extract as assets


def requested_icons(regulation):
    # The editor also requests the two empty-slot illustrations when available.
    result = {('Item', 19200), ('Item', 19201)}
    antiques = regulation['EquipParamAntique.param']
    for iid, at in assets.checked_rows(antiques, 2, 48):
        row = antiques[at : at + 48]
        if (
            iid >= 100
            and not row[0] & 1
            and row[4] <= 3
            and any(table != -1 for table in struct.unpack_from('<3i', row, 16))
        ):
            result.add(('Item', assets.number(row, 'I', 28)))
    effects = regulation['AttachEffectParam.param']
    for _, at in assets.checked_rows(effects, 3, 68):
        icon = assets.number(effects, 'i', at + 40)
        if icon >= 0:
            result.add(('Status', icon))
    return result


def icon_rgba(image, rect):
    x, y, width, height = (int(rect[field]) for field in ('x', 'y', 'width', 'height'))
    assets.require(
        x >= 0
        and y >= 0
        and width > 0
        and height > 0
        and x + width <= image.width
        and y + height <= image.height,
        'Icon rectangle outside its DDS image',
    )
    crop = image.crop((x, y, x + width, y + height))
    if crop.size == (160, 160):
        return crop.tobytes()

    # Match the editor's center-sampled bilinear resize and half-up byte rounding.
    # Pillow's resize filters use different downsampling and alpha rules.
    def coordinates(length):
        return [max(0.0, min((i + 0.5) * length / 160 - 0.5, length - 1.0)) for i in range(160)]

    pixels = crop.load()
    result = bytearray()
    for sy in coordinates(height):
        y0, fy = int(sy), sy - int(sy)
        y1 = min(y0 + 1, height - 1)
        for sx in coordinates(width):
            x0, fx = int(sx), sx - int(sx)
            x1 = min(x0 + 1, width - 1)
            for channel in range(4):
                top = pixels[x0, y0][channel] * (1 - fx) + pixels[x1, y0][channel] * fx
                bottom = pixels[x0, y1][channel] * (1 - fx) + pixels[x1, y1][channel] * fx
                result.append(math.floor(top * (1 - fy) + bottom * fy + 0.5))
    return bytes(result)


def verify(game, regulation, probe, output, override, cache):
    output.mkdir(parents=True, exist_ok=True)
    subprocess.run([str(probe), str(override or game), str(regulation), str(output)], check=True)
    extractor = assets.Extractor(game, cache)
    wanted = requested_icons(extractor.regulation(regulation))
    paths = [
        '/msg/engus/item_dlc01.msgbnd.dcx',
        '/menu/01_common_h.sblytbnd.dcx',
        '/menu/01_common_h.tpf.dcx',
        '/menu/21_common_dlc01_h.sblytbnd.dcx',
        '/menu/21_common_dlc01_h.tpf.dcx',
    ]
    files = {}
    if override:
        files.update(
            (path, (override / path[1:]).read_bytes())
            for path in paths
            if (override / path[1:]).is_file()
        )
    missing = [path for path in paths if path not in files]
    files.update(extractor.archive_files(missing))
    item_names, effect_names = {}, {}
    for name, blob in sorted(extractor.binder(files[paths[0]]).items()):
        target = (
            item_names
            if name.startswith('AntiqueName')
            else effect_names
            if name.startswith('AttachEffectName')
            else None
        )
        if target is not None:
            target.update(
                (eid, text.replace('\t', ' ').replace('\r', ' ').replace('\n', ' '))
                for eid, text in assets.messages(blob)
            )
    native_items, native_effects, cells = {}, {}, {}
    width = height = 0
    for line in (output / 'assets.tsv').read_text(encoding='utf-8').splitlines():
        parts = line.split('\t')
        if parts[0] == 'ATLAS':
            width, height = map(int, parts[1:])
        elif parts[0] == 'ITEM':
            native_items[int(parts[1])] = parts[2] if len(parts) > 2 else ''
        elif parts[0] == 'EFFECT':
            native_effects[int(parts[1])] = parts[2] if len(parts) > 2 else ''
        elif parts[0] == 'ICON':
            kind, eid, cell = map(int, parts[1:])
            cells['Item' if kind == 0 else 'Status', eid] = cell
    assert native_items == item_names, 'Native item names differ from FMG messages'
    assert native_effects == effect_names, 'Native effect names differ from FMG messages'
    expected = {}
    for prefix in ('01_common_h', '21_common_dlc01_h'):
        layouts = extractor.binder(files[f'/menu/{prefix}.sblytbnd.dcx'])
        textures = dict(assets.textures(extractor.decompress(files[f'/menu/{prefix}.tpf.dcx'])))
        for name, xml in layouts.items():
            if not name.endswith('.layout'):
                continue
            rects = []
            for entry in ET.fromstring(xml).iter():
                if entry.tag.rsplit('}', 1)[-1] != 'SubTexture':
                    continue
                icon = entry.attrib.get('name', '')
                for kind in ('Item', 'Status'):
                    marker = f'MENU_{kind}Icon_'
                    if icon.startswith(marker):
                        suffix = Path(icon.removeprefix(marker)).stem
                        if (
                            not suffix.isascii()
                            or not suffix.isdecimal()
                            or int(suffix) > 0xFFFFFFFF
                        ):
                            continue
                        key = kind, int(suffix)
                        if key in wanted:
                            rects.append((key, entry.attrib))
            if not rects:
                continue
            image = Image.open(io.BytesIO(textures[name.removesuffix('.layout')])).convert('RGBA')
            for key, rect in rects:
                expected[key] = icon_rgba(image, rect)
    assert expected, 'No requested icon crops were checked'
    assert cells.keys() == expected.keys(), (
        f'Native icon coverage differs. Missing: {sorted(expected.keys() - cells.keys())}; '
        f'extra: {sorted(cells.keys() - expected.keys())}'
    )
    native_atlas = Image.frombytes('RGBA', (width, height), (output / 'atlas.rgba').read_bytes())
    for key, rgba in expected.items():
        cell = cells[key]
        x, y = cell % 16 * 164 + 2, cell // 16 * 164 + 2
        assert native_atlas.crop((x, y, x + 160, y + 160)).tobytes() == rgba, (
            f'Native DDS crop differs for {key}'
        )
    summary = {
        'item_names': len(item_names),
        'effect_names': len(effect_names),
        'icons': len(cells),
        'exact_rgba_crops_checked': len(expected),
        'source': str(override or game),
    }
    (output / 'report.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', type=Path, required=True)
    parser.add_argument('--regulation', type=Path)
    parser.add_argument('--override-dir', type=Path)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    with TemporaryDirectory() as cache:
        verify(
            args.game_dir.resolve(),
            (args.regulation or args.game_dir / 'regulation.bin').resolve(),
            args.probe.resolve(),
            args.output.resolve(),
            args.override_dir.resolve() if args.override_dir else None,
            Path(cache),
        )
