"""Read installed Nightreign archives and export relic metadata and authentic icons.

No game files or saves are modified. Requires Pillow, cryptography, zstandard.
Format reference: Smithbox c63eae720c00ffa48abad9b77906d0ed4a9f81e7,
Andre.Formats (MIT) and SoulsFormats (GPL-3.0), see ThirdPartyNotices.txt. Only selected menu/message files
are read from the archives; the full game is never unpacked.
"""

import argparse
import ctypes
import hashlib
import io
import json
import struct
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path
from tempfile import TemporaryDirectory

import zstandard
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives.serialization import load_pem_public_key
from PIL import Image

EMPTY = 0xFFFFFFFF
CELL = 164  # 160px icon with a two-pixel gutter, preventing texture bleeding.
COLUMNS = 16


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(data, fmt, offset):
    return struct.unpack_from('<' + fmt, data, offset)[0]


def string(data, offset, wide=False):
    require(0 <= offset < len(data), 'String offset outside file')
    if not wide:
        return data[offset : data.index(0, offset)].decode('utf-8')
    end = offset
    while end + 1 < len(data) and data[end : end + 2] != b'\0\0':
        end += 2
    return data[offset:end].decode('utf-16-le')


class Extractor:
    def __init__(self, game, cache):
        self.game, self.cache = game, cache
        self.keys = json.loads((Path(__file__).parent / 'nightreign-keys.json').read_text())
        self.oodle = ctypes.CDLL(str(game / 'oo2core_9_win64.dll'))
        self.oodle.OodleLZ_Decompress.restype = ctypes.c_ssize_t
        self.oodle.OodleLZ_Decompress.argtypes = [
            ctypes.c_void_p,
            ctypes.c_ssize_t,
            ctypes.c_void_p,
            ctypes.c_ssize_t,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_int,
            ctypes.c_void_p,
            ctypes.c_ssize_t,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_void_p,
            ctypes.c_ssize_t,
            ctypes.c_int,
        ]

    def decompress(self, data):
        if not data.startswith(b'DCX\0'):
            return data
        require(len(data) >= 0x2C, 'Truncated DCX header')
        size, packed = struct.unpack_from('>II', data, 0x1C)
        require(0 < size <= 1024 * 1024 * 1024, 'Unsupported DCX size')
        dca = data.find(b'DCA\0', 0x2C, 256)
        require(
            dca >= 0 and dca + 8 <= len(data) and struct.unpack_from('>I', data, dca + 4)[0] == 8,
            'Invalid DCX payload header',
        )
        start = dca + 8
        require(packed <= len(data) - start, 'Truncated DCX payload')
        payload = data[start : start + packed]
        algo = data[0x28:0x2C]
        if algo == b'DFLT':
            result = zlib.decompress(payload)
        elif algo == b'ZSTD':
            result = zstandard.ZstdDecompressor().decompress(payload, max_output_size=size)
        else:
            require(algo == b'KRAK', f'Unsupported DCX codec {algo!r}')
            dest = ctypes.create_string_buffer(size)
            source = ctypes.create_string_buffer(payload)
            actual = self.oodle.OodleLZ_Decompress(
                source, len(payload), dest, size, 1, 0, 0, None, 0, None, None, None, 0, 3
            )
            require(actual == size, 'Oodle decompression failed')
            result = dest.raw
        require(len(result) == size, 'DCX length mismatch')
        return result

    def binder(self, data):
        data = self.decompress(data)
        require(data[:4] == b'BND4' and data[9] == 0, 'Unsupported binder format')
        count, stride = number(data, 'I', 12), number(data, 'Q', 32)
        fmt = data[49]
        if data[10] and not (fmt & 1 and not fmt & 128):
            fmt = int(f'{fmt:08b}'[::-1], 2)
        result = {}
        for index in range(count):
            pos = 64 + index * stride
            size = number(data, 'Q', pos + 8)
            field = pos + 16 + (8 if fmt & 32 else 0)
            offset = number(data, 'Q' if fmt & 16 else 'I', field)
            field += 8 if fmt & 16 else 4
            if fmt & 2:
                field += 4
            name = (
                string(data, number(data, 'I', field), bool(data[48]))
                .replace('\\', '/')
                .split('/')[-1]
            )
            require(offset + size <= len(data), 'Binder entry exceeds file')
            result[name] = self.decompress(data[offset : offset + size])
        return result

    def regulation(self, path=None):
        data = (path if path is not None else self.game / 'regulation.bin').read_bytes()
        decrypt = Cipher(
            algorithms.AES(bytes.fromhex(self.keys['regulation'])), modes.CBC(data[:16])
        ).decryptor()
        return self.binder(decrypt.update(data[16:]) + decrypt.finalize())

    def archive_files(self, wanted):
        remaining = set(wanted)
        for archive in ('data0', 'data1', 'data2', 'data3', 'dlc01'):
            if not remaining:
                break
            encrypted = (self.game / (archive + '.bhd')).read_bytes()
            digest = hashlib.sha256(encrypted).hexdigest()
            cache = self.cache / (digest + '.bhd')
            if cache.exists():
                header = cache.read_bytes()
            else:
                key = load_pem_public_key(self.keys['archives'][archive].encode()).public_numbers()
                header = b''.join(
                    pow(int.from_bytes(encrypted[i : i + 256], 'big'), key.e, key.n).to_bytes(
                        255, 'big'
                    )
                    for i in range(0, len(encrypted), 256)
                )
                self.cache.mkdir(parents=True, exist_ok=True)
                cache.write_bytes(header)
            require(header.startswith(b'BHD5'), 'Archive index did not decrypt')
            count, offset = struct.unpack_from('<II', header, 16)
            require(offset != 0, 'Unsupported 64-bit BHD index')
            targets = {path_hash(path): path for path in remaining}
            with (self.game / (archive + '.bdt')).open('rb') as stream:
                for bucket in range(count):
                    length, at = struct.unpack_from('<II', header, offset + bucket * 8)
                    for index in range(length):
                        file_hash, size, unpadded, start, _, aes = struct.unpack_from(
                            '<Qiiqqq', header, at + index * 40
                        )
                        if file_hash not in targets:
                            continue
                        stream.seek(start)
                        blob = bytearray(stream.read(size))
                        require(len(blob) == size, 'Truncated archive file')
                        if aes:
                            key = header[aes : aes + 16]
                            for j in range(number(header, 'i', aes + 16)):
                                left, right = struct.unpack_from('<qq', header, aes + 20 + j * 16)
                                if left == -1 or right == -1:
                                    continue
                                decrypt = Cipher(algorithms.AES(key), modes.ECB()).decryptor()
                                blob[left:right] = (
                                    decrypt.update(blob[left:right]) + decrypt.finalize()
                                )
                        path = targets[file_hash]
                        remaining.remove(path)
                        yield path, bytes(blob[: unpadded if unpadded > 0 else size])
        require(not remaining, f'Game assets not found: {sorted(remaining)}')


def path_hash(path):
    result = 0
    for char in path.lower().replace('\\', '/'):
        result = (result * 0x85 + ord(char)) & 0xFFFFFFFFFFFFFFFF
    return result


def param_rows(data):
    require(data[44] == 0 and data[45] == 0x85, 'Unsupported PARAM layout')
    for index in range(number(data, 'H', 10)):
        pos = 64 + index * 24
        yield number(data, 'i', pos), number(data, 'Q', pos + 8)


def checked_rows(data, version, size):
    require(number(data, 'H', 8) == version, 'Unsupported PARAM data version')
    rows = list(param_rows(data))
    end = number(data, 'I', 0)
    for index, (row_id, offset) in enumerate(rows):
        following = rows[index + 1][1] if index + 1 < len(rows) else end
        require(
            following - offset == size and offset + size <= len(data), 'Unsupported PARAM row size'
        )
        yield row_id, offset


def messages(data):
    require(data[2] == 2 and data[8] == 1, 'Unsupported FMG layout')
    offsets = number(data, 'Q', 24)
    for group in range(number(data, 'i', 12)):
        index, first, last = struct.unpack_from('<iii', data, 40 + group * 16)
        for j in range(last - first + 1):
            offset = number(data, 'Q', offsets + 8 * (index + j))
            if offset:
                yield first + j, string(data, offset, True)


def textures(data):
    require(data.startswith(b'TPF\0') and data[12] == 0, 'Unsupported TPF platform')
    for index in range(number(data, 'I', 8)):
        at = 16 + index * 20
        require(number(data, 'I', at + 16) == 0, 'Unsupported TPF extended header')
        offset, size = struct.unpack_from('<II', data, at)
        name = string(data, number(data, 'I', at + 12), data[14] == 1)
        yield name, data[offset : offset + size]


def export(game, output, cache):
    extractor = Extractor(game, cache)
    source_hash = hashlib.sha256((game / 'regulation.bin').read_bytes()).hexdigest()
    regulation = extractor.regulation()
    antique = regulation['EquipParamAntique.param']
    effect_data = regulation['AttachEffectParam.param']
    tables_data = regulation['AttachEffectTableParam.param']
    tables = {}
    for table_id, offset in checked_rows(tables_data, 1, 12):
        tables.setdefault(table_id, []).append(struct.unpack_from('<iiHh', tables_data, offset))
    paths = [
        '/msg/engus/item_dlc01.msgbnd.dcx',
        '/menu/01_common_h.sblytbnd.dcx',
        '/menu/01_common_h.tpf.dcx',
        '/menu/21_common_dlc01_h.sblytbnd.dcx',
        '/menu/21_common_dlc01_h.tpf.dcx',
    ]
    files = dict(extractor.archive_files(paths))
    names = {}
    effect_names = {}
    for name, data in extractor.binder(files[paths[0]]).items():
        if name.startswith('AntiqueName'):
            names.update(messages(data))
        elif name.startswith('AttachEffectName'):
            effect_names.update(messages(data))
    items = []
    for item_id, offset in checked_rows(antique, 2, 48):
        row = antique[offset : offset + 48]
        if row[0] & 1 or item_id not in names or item_id < 100:
            continue
        effect_tables = struct.unpack_from('<3i', row, 16)
        active = [table for table in effect_tables if table != -1]
        if not active or row[4] > 3:
            continue

        # A rolled item must be salable AND every slot must reference a lottery.
        # Fixed rewards cannot become editable by changing their instance effects.
        def candidates(table):
            return {
                entry[1]
                for entry in tables.get(table, [])
                if entry[1] >= 0 and (entry[2] > 0 or entry[3] > 0)
            }

        rolled = bool(row[6]) and all(len(candidates(table)) > 1 for table in active)
        kind = 'deep' if row[7] & 1 else 'rolled' if rolled else 'unique' if not row[6] else 'fixed'
        fixed = []
        for table in effect_tables:
            choices = candidates(table)
            fixed.append(next(iter(choices)) if len(choices) == 1 else EMPTY)
        items.append(
            {
                'id': item_id,
                'kind': kind,
                'color': row[4],
                'icon': number(row, 'I', 28),
                'effects': fixed,
                'tables': effect_tables,
                'name': names[item_id].replace('\t', ' ').replace('\r', ' ').replace('\n', ' '),
            }
        )
    effects = []
    for effect_id, offset in checked_rows(effect_data, 3, 68):
        text_id = number(effect_data, 'i', offset + 28)
        description = (
            effect_names.get(text_id, '').replace('\t', ' ').replace('\r', ' ').replace('\n', ' ')
        )
        effects.append(
            {
                'id': effect_id,
                'compatibility': number(effect_data, 'i', offset + 24),
                'order': number(effect_data, 'i', offset + 52),
                'icon': number(effect_data, 'i', offset + 40),
                'description': description,
            }
        )
    # Keep the two icon namespaces separate even when their numeric IDs match.
    relic_icons = sorted({item['icon'] for item in items})
    effect_icons = sorted({effect['icon'] for effect in effects if effect['icon'] >= 0})
    icon_keys = [('Item', icon) for icon in relic_icons] + [
        ('Status', icon) for icon in effect_icons
    ]
    crops = {}
    for prefix in ('01_common_h', '21_common_dlc01_h'):
        layouts = extractor.binder(files[f'/menu/{prefix}.sblytbnd.dcx'])
        images = dict(textures(extractor.decompress(files[f'/menu/{prefix}.tpf.dcx'])))
        for name, layout in layouts.items():
            root = ET.fromstring(layout)
            matches = []
            for entry in root:
                icon = entry.attrib.get('name', '')
                for kind in ('Item', 'Status'):
                    prefix_name = f'MENU_{kind}Icon_'
                    if icon.startswith(prefix_name):
                        key = (kind, int(Path(icon.removeprefix(prefix_name)).stem))
                        if key in icon_keys:
                            matches.append((key, entry.attrib))
            if not matches:
                continue
            image = Image.open(io.BytesIO(images[name.removesuffix('.layout')])).convert('RGBA')
            for key, rect in matches:
                x, y, width, height = (int(rect[key]) for key in ('x', 'y', 'width', 'height'))
                crops[key] = image.crop((x, y, x + width, y + height)).resize(
                    (160, 160), Image.Resampling.LANCZOS
                )
    require(set(icon_keys) <= crops.keys(), f'Missing icons: {set(icon_keys) - crops.keys()}')
    atlas = Image.new('RGBA', (COLUMNS * CELL, ((len(icon_keys) + COLUMNS - 1) // COLUMNS) * CELL))
    for cell, key in enumerate(icon_keys):
        atlas.paste(crops[key], (cell % COLUMNS * CELL + 2, cell // COLUMNS * CELL + 2))
    output.mkdir(parents=True, exist_ok=True)
    atlas.save(output / 'relic-icons.png')
    cells = {key: cell for cell, key in enumerate(icon_keys)}
    lines = [f'ERN_RELICS\t2\t{source_hash}']
    for item in items:
        lines.append(
            '\t'.join(
                map(
                    str,
                    [
                        item['id'],
                        item['kind'],
                        item['color'],
                        item['icon'],
                        cells['Item', item['icon']],
                        *item['effects'],
                        *item['tables'],
                        item['name'],
                    ],
                )
            )
        )
    (output / 'relics.tsv').write_text('\n'.join(lines) + '\n', encoding='utf-8', newline='\n')
    effect_lines = [f'ERN_EFFECTS\t2\t{source_hash}']
    for effect in effects:
        effect_lines.append(
            '\t'.join(
                map(
                    str,
                    [
                        effect['id'],
                        effect['compatibility'],
                        effect['order'],
                        cells.get(('Status', effect['icon']), -1),
                        effect['description'],
                    ],
                )
            )
        )
    (output / 'effects.tsv').write_text(
        '\n'.join(effect_lines) + '\n', encoding='utf-8', newline='\n'
    )
    rules = [f'ERN_RELIC_RULES\t1\t{source_hash}']
    used_tables = {table for item in items for table in item['tables'] if table != -1}
    for table_id in sorted(used_tables):
        for _, effect_id, base, dlc in tables.get(table_id, []):
            rules.append(f'{table_id}\t{effect_id}\t{base}\t{dlc}')
    (output / 'relic-rules.tsv').write_text('\n'.join(rules) + '\n', encoding='utf-8', newline='\n')
    require(
        hashlib.sha256((game / 'regulation.bin').read_bytes()).hexdigest() == source_hash,
        'Regulation changed during extraction. Run the extractor again.',
    )
    digests = {
        name: hashlib.sha256((output / name).read_bytes()).hexdigest()
        for name in ('effects.tsv', 'relics.tsv', 'relic-rules.tsv')
    }
    (output / 'relic-data.sha256').write_text(
        ''.join(f'{digest}  {name}\n' for name, digest in digests.items()),
        encoding='ascii',
        newline='\n',
    )
    manifest = {
        'source': 'Installed Nightreign game files',
        'regulation_sha256': source_hash,
        'items': len(items),
        'effects': len(effects),
        'pool_entries': len(rules) - 1,
        'icons': len(icon_keys),
        'relic_icons': len(relic_icons),
        'effect_icons': len(effect_icons),
        'atlas_cell': CELL,
        'atlas_columns': COLUMNS,
        'missing_tables': sorted(used_tables - tables.keys()),
        'files': digests,
    }
    (output / 'relic-assets.json').write_text(
        json.dumps(manifest, indent=2) + '\n', encoding='utf-8', newline='\n'
    )
    print(
        f'Exported {len(items)} relics, {len(effects)} effects, {len(rules) - 1} pool entries and {len(icon_keys)} icons to {output}'
    )


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', required=True, type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cache', type=Path)
    args = parser.parse_args()
    if args.cache:
        export(args.game_dir.resolve(), args.output.resolve(), args.cache.resolve())
    else:
        with TemporaryDirectory() as cache:
            export(args.game_dir.resolve(), args.output.resolve(), Path(cache))
