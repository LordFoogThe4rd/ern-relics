"""Check independent asset audits using generated files and the native probes.

Run with uv run --locked --group assets python -m tests.tooling.assets_tests --build-directory <directory>.
No game installation, proprietary decoder, or redistributed game assets are used.
"""

import argparse
import contextlib
import ctypes
import io
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from PIL import Image

from tools.assets import extract, verify_assets, verify_regulation

ROOT = Path(__file__).resolve().parents[2]
PROBES = None


def param(version, rows):
    result = bytearray(64 + len(rows) * 24)
    struct.pack_into('<HH', result, 8, version, len(rows))
    result[45] = 0x85
    for index, (key, row) in enumerate(rows):
        struct.pack_into('<i', result, 64 + index * 24, key)
        struct.pack_into('<Q', result, 72 + index * 24, len(result))
        result.extend(row)
    struct.pack_into('<I', result, 0, len(result))
    return result


def binder(entries):
    result = bytearray(64 + len(entries) * 36)
    result[:4] = b'BND4'
    struct.pack_into('<I', result, 12, len(entries))
    struct.pack_into('<Q', result, 32, 36)
    result[49] = 0x2E
    for index, (name, data) in enumerate(entries):
        pos = 64 + index * 36
        struct.pack_into('<Q', result, pos + 8, len(data))
        struct.pack_into('<I', result, pos + 32, len(result))
        result.extend(name.encode() + b'\0')
        struct.pack_into('<I', result, pos + 24, len(result))
        result.extend(data)
    return result


def item(deep=True, salable=True, tables=(40, -1, -1), curses=(50, -1, -1), icon=19108):
    row = bytearray(48)
    row[6:8] = bytes([salable, deep])
    struct.pack_into('<3iI3i', row, 16, *tables, icon, *curses)
    return row


def effect(eid):
    row = bytearray(68)
    for offset, value in ((24, -1), (28, eid), (52, -eid), (60, -1)):
        struct.pack_into('<i', row, offset, value)
    return row


def regulation(directory, items=None, pools=None):
    if items is None:
        items = [(2000010, item())]
    if pools is None:
        pools = [(40, 101, 1, -1), (50, 901, 1, -1)]
    data = binder(
        [
            ('EquipParamAntique.param', param(2, items)),
            ('AttachEffectParam.param', param(3, [(eid, effect(eid)) for eid in (101, 901, 902)])),
            (
                'AttachEffectTableParam.param',
                param(
                    1,
                    [
                        (tid, struct.pack('<iiHh', 0, eid, base, dlc))
                        for tid, eid, base, dlc in pools
                    ],
                ),
            ),
        ]
    )
    data.extend(bytes(-len(data) % 16))
    keys = json.loads((ROOT / 'tools/assets/nightreign-keys.json').read_text())
    iv = bytes(16)
    encoder = Cipher(algorithms.AES(bytes.fromhex(keys['regulation'])), modes.CBC(iv)).encryptor()
    target = directory / 'regulation.bin'
    target.write_bytes(iv + encoder.update(data) + encoder.finalize())
    return target


def unused_oodle():
    return SimpleNamespace(OodleLZ_Decompress=Mock())


def fmg(eid, text):
    result = bytearray(64)
    result[2], result[8] = 2, 1
    struct.pack_into('<i', result, 12, 1)
    struct.pack_into('<Q', result, 24, 56)
    struct.pack_into('<iii', result, 40, 0, eid, eid)
    struct.pack_into('<Q', result, 56, len(result))
    result.extend(text.encode('utf-16-le') + b'\0\0')
    return result


def texture(image):
    # Synthetic DX10 RGBA8 DDS, supported by both native decoding and Pillow.
    dds = bytearray(148)
    dds[:4] = b'DDS '
    struct.pack_into('<I', dds, 4, 124)
    struct.pack_into('<II', dds, 12, image.height, image.width)
    struct.pack_into('<II', dds, 76, 32, 4)
    dds[84:88] = b'DX10'
    struct.pack_into('<5I', dds, 128, 28, 3, 0, 1, 0)
    dds.extend(image.tobytes())
    result = bytearray(36)
    result[:4] = b'TPF\0'
    struct.pack_into('<I', result, 8, 1)
    struct.pack_into('<I', result, 28, len(result))
    result.extend(b'atlas\0')
    struct.pack_into('<II', result, 16, len(result), len(dds))
    result.extend(dds)
    return result


def menu_assets(directory):
    (directory / 'msg/engus').mkdir(parents=True)
    (directory / 'menu').mkdir()
    (directory / 'msg/engus/item_dlc01.msgbnd.dcx').write_bytes(
        binder(
            [
                ('AntiqueName.fmg', fmg(2000010, 'Invented item')),
                ('AttachEffectName.fmg', fmg(101, 'Invented effect')),
            ]
        )
    )
    xml = (
        b'<TextureAtlas><Group>'
        b'<SubTexture name="MENU_ItemIcon_19108.png" x="0" y="0" width="160" height="160"/>'
        b'<SubTexture name="MENU_StatusIcon_0.png" x="160" y="0" width="3" height="2"/>'
        b'<SubTexture name="MENU_ItemIcon_19200.png" x="164" y="0" width="320" height="160"/>'
        b'<SubTexture name="MENU_ItemIcon_33333.png" x="484" y="0" width="160" height="160"/>'
        b'</Group></TextureAtlas>'
    )
    (directory / 'menu/01_common_h.sblytbnd.dcx').write_bytes(binder([('atlas.layout', xml)]))
    image = Image.new('RGBA', (644, 160))
    image.putdata(
        [
            (
                (x * 13 + y * 7) % 256,
                (x + y * 17) % 256,
                (x * 3 + y * 5) % 256,
                (x * 11 + y * 3) % 256,
            )
            for y in range(image.height)
            for x in range(image.width)
        ]
    )
    (directory / 'menu/01_common_h.tpf.dcx').write_bytes(texture(image))
    (directory / 'menu/21_common_dlc01_h.sblytbnd.dcx').write_bytes(binder([]))
    empty = bytearray(16)
    empty[:4] = b'TPF\0'
    (directory / 'menu/21_common_dlc01_h.tpf.dcx').write_bytes(empty)


class SyntheticFiles(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='asset-tests-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)


class ExtractorTests(SyntheticFiles):
    def test_truncated_kraken_never_calls_native_decoder(self):
        data = bytearray(53)
        data[:4] = b'DCX\0'
        struct.pack_into('>II', data, 0x1C, 8, 4096)
        data[0x28:0x30] = b'KRAKDCA\0'
        struct.pack_into('>I', data, 0x30, 8)
        instance = extract.Extractor.__new__(extract.Extractor)
        instance.oodle = unused_oodle()
        for truncated in (data[:4], data[:48], data[:51], data):
            with self.subTest(length=len(truncated)), self.assertRaisesRegex(ValueError, 'DCX'):
                instance.decompress(bytes(truncated))
        malformed = bytearray(data)
        struct.pack_into('>I', malformed, 0x30, 7)
        with self.assertRaisesRegex(ValueError, 'payload header'):
            instance.decompress(bytes(malformed))
        instance.oodle.OodleLZ_Decompress.assert_not_called()

        struct.pack_into('>I', data, 0x20, 1)
        instance.oodle.OodleLZ_Decompress.return_value = 8
        self.assertEqual(instance.decompress(bytes(data)), bytes(8))
        arguments = instance.oodle.OodleLZ_Decompress.call_args.args
        self.assertEqual(arguments[1], 1)
        self.assertGreaterEqual(ctypes.sizeof(arguments[0]), arguments[1])

    def test_archive_creates_nested_cache_then_reuses_it(self):
        path = '/fixture'
        header = bytearray(255)
        header[:4] = b'BHD5'
        struct.pack_into('<II', header, 16, 1, 24)
        struct.pack_into('<II', header, 24, 1, 32)
        struct.pack_into('<Qiiqqq', header, 32, extract.path_hash(path), 4, 4, 0, 0, 0)
        (self.root / 'data0.bhd').write_bytes(b'\0' + header)
        (self.root / 'data0.bdt').write_bytes(b'data')
        instance = extract.Extractor.__new__(extract.Extractor)
        instance.game, instance.cache = self.root, self.root / 'absent/cache'
        instance.keys = {'archives': {'data0': 'synthetic key'}}
        key = SimpleNamespace(public_numbers=lambda: SimpleNamespace(e=1, n=(1 << 2048) - 1))
        with patch.object(extract, 'load_pem_public_key', return_value=key) as decrypt:
            self.assertEqual(list(instance.archive_files([path])), [(path, b'data')])
            decrypt.assert_called_once()
        self.assertEqual(len(list(instance.cache.glob('*.bhd'))), 1)
        with patch.object(
            extract, 'load_pem_public_key', side_effect=AssertionError('Cache not reused')
        ):
            self.assertEqual(list(instance.archive_files([path])), [(path, b'data')])


class NativeAuditTests(SyntheticFiles):
    def probe(self, name):
        result = PROBES / f'ern_relics_{name}_probe.exe'
        self.assertTrue(
            result.is_file(), 'Build the native probes before running asset regressions'
        )
        return result

    def test_fixed_deep_curse_oracle_agrees_with_real_probe(self):
        items = [
            (2000010, item()),
            (2000011, item(salable=False)),
            (2000012, item(curses=(51, -1, -1))),
            (2000013, item(curses=(52, -1, -1))),
            (2000014, item(curses=(53, -1, -1))),
            (2000015, item(curses=(-1, 50, -1))),
            (1003005, item(deep=False)),
        ]
        pools = [
            (40, 101, 1, -1),
            (50, 901, 1, -1),
            (50, 902, 0, 0),
            (51, 901, 1, -1),
            (51, -1, 1, -1),
            (52, 901, 1, 0),
            (52, 902, 0, 1),
            (53, 901, 0, 0),
        ]
        source = regulation(self.root, items, pools)
        report = self.root / 'rules-report.json'
        with (
            patch.object(extract.ctypes, 'CDLL', return_value=unused_oodle()),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            verify_regulation.verify(self.root, source, self.probe('rules'), report)
        self.assertEqual(json.loads(report.read_text())['relic_definitions'], len(items))

    def audit_icons(self, mutate=None):
        source = regulation(self.root)
        menu_assets(self.root)
        output = self.root / 'output'
        run = subprocess.run

        def run_probe(command, **kwargs):
            result = run(command, **kwargs)
            if mutate:
                mutate(output)
            return result

        with (
            patch.object(extract.ctypes, 'CDLL', return_value=unused_oodle()),
            patch.object(verify_assets.subprocess, 'run', side_effect=run_probe),
            contextlib.redirect_stdout(io.StringIO()),
        ):
            verify_assets.verify(
                self.root, source, self.probe('assets'), output, self.root, self.root / 'cache'
            )
        return json.loads((output / 'report.json').read_text())

    def test_native_icon_coverage_and_bilinear_pixels(self):
        # Includes available empty-slot art and ignores both unavailable art and
        # unrelated layout icons. The status and empty-slot crops test bilinear
        # enlargement, reduction, alpha, and half-up rounding.
        report = self.audit_icons()
        self.assertEqual(report['icons'], 3)
        self.assertEqual(report['exact_rgba_crops_checked'], 3)

    def native_icons(self, source, name):
        output = self.root / name
        result = subprocess.run(
            [str(self.probe('assets')), str(self.root), str(source), str(output)],
            capture_output=True,
            text=True,
            encoding='utf-8',
            check=True,
        )
        icons = {
            (int(fields[1]), int(fields[2]))
            for line in (output / 'assets.tsv').read_text(encoding='utf-8').splitlines()
            if (fields := line.split('\t'))[0] == 'ICON'
        }
        return icons, result.stderr

    def test_bad_layout_preserves_independent_icons_in_both_binder_orders(self):
        source = regulation(self.root)
        menu_assets(self.root)
        good = (
            b'<TextureAtlas><SubTexture name="MENU_ItemIcon_19108.png" '
            b'x="0" y="0" width="160" height="160"/></TextureAtlas>'
        )
        missing = (
            b'<TextureAtlas><SubTexture name="MENU_StatusIcon_0.png" '
            b'x="0" y="0" width="1" height="1"/></TextureAtlas>'
        )
        entries = [('atlas.layout', good), ('missing.layout', missing)]
        for reverse in (False, True):
            with self.subTest(reverse=reverse):
                ordered = entries[::-1] if reverse else entries
                (self.root / 'menu/01_common_h.sblytbnd.dcx').write_bytes(binder(ordered))
                icons, warnings = self.native_icons(source, f'layout-output-{reverse}')
                self.assertEqual(icons, {(0, 19108)})
                self.assertIn('missing.layout', warnings)
                self.assertIn('missing texture', warnings)

    def test_bad_message_table_preserves_independent_item_and_effect_names(self):
        source = regulation(self.root)
        menu_assets(self.root)
        (self.root / 'msg/engus/item_dlc01.msgbnd.dcx').write_bytes(
            binder(
                [
                    ('AntiqueName.fmg', b'bad'),
                    ('AntiqueName_dlc01.fmg', fmg(2000010, 'Remaining item')),
                    ('AttachEffectName.fmg', fmg(101, 'Remaining effect')),
                ]
            )
        )
        output = self.root / 'message-output'
        result = subprocess.run(
            [str(self.probe('assets')), str(self.root), str(source), str(output)],
            capture_output=True,
            text=True,
            encoding='utf-8',
            check=True,
        )
        metadata = (output / 'assets.tsv').read_text(encoding='utf-8').splitlines()
        self.assertIn('ITEM\t2000010\tRemaining item', metadata)
        self.assertIn('EFFECT\t101\tRemaining effect', metadata)
        self.assertIn('Messages: AntiqueName.fmg:', result.stderr)
        self.assertIn('Unsupported FMG layout', result.stderr)

    def test_bad_rectangle_preserves_independent_icons_in_both_row_orders(self):
        source = regulation(self.root)
        menu_assets(self.root)
        good = b'<SubTexture name="MENU_ItemIcon_19108.png" x="0" y="0" width="160" height="160"/>'
        outside = b'<SubTexture name="MENU_StatusIcon_0.png" x="643" y="0" width="2" height="1"/>'
        for reverse in (False, True):
            with self.subTest(reverse=reverse):
                rows = good + outside if reverse else outside + good
                xml = b'<TextureAtlas>' + rows + b'</TextureAtlas>'
                (self.root / 'menu/01_common_h.sblytbnd.dcx').write_bytes(
                    binder([('atlas.layout', xml)])
                )
                icons, warnings = self.native_icons(source, f'rect-output-{reverse}')
                self.assertEqual(icons, {(0, 19108)})
                self.assertIn('atlas.layout, status icon 0', warnings)
                self.assertIn('outside its DDS image', warnings)

    def test_bad_rectangle_attributes_preserve_independent_icons_in_both_row_orders(self):
        source = regulation(self.root)
        menu_assets(self.root)
        good = b'<SubTexture name="MENU_ItemIcon_19108.png" x="0" y="0" width="160" height="160"/>'
        for case, attributes, message in (
            ('invalid', b'x="160" y="0" width="bad" height="1"', 'Invalid texture rectangle'),
            ('missing', b'x="160" y="0" height="1"', 'missing a coordinate'),
        ):
            bad = b'<SubTexture name="MENU_StatusIcon_0.png" ' + attributes + b'/>'
            for reverse in (False, True):
                with self.subTest(case=case, reverse=reverse):
                    rows = good + bad if reverse else bad + good
                    xml = b'<TextureAtlas>' + rows + b'</TextureAtlas>'
                    (self.root / 'menu/01_common_h.sblytbnd.dcx').write_bytes(
                        binder([('atlas.layout', xml)])
                    )
                    icons, warnings = self.native_icons(source, f'attr-output-{case}-{reverse}')
                    self.assertEqual(icons, {(0, 19108)})
                    self.assertIn('atlas.layout, status icon 0', warnings)
                    self.assertIn(message, warnings)

    def test_malformed_xml_does_not_publish_partial_layout(self):
        source = regulation(self.root)
        menu_assets(self.root)
        xml = (
            b'<TextureAtlas><SubTexture name="MENU_ItemIcon_19108.png" '
            b'x="0" y="0" width="160" height="160"/><Unclosed>'
        )
        (self.root / 'menu/01_common_h.sblytbnd.dcx').write_bytes(binder([('atlas.layout', xml)]))
        output = self.root / 'malformed-xml-output'
        result = subprocess.run(
            [str(self.probe('assets')), str(self.root), str(source), str(output)],
            capture_output=True,
            text=True,
            encoding='utf-8',
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertNotIn('ICON\t', (output / 'assets.tsv').read_text(encoding='utf-8'))
        self.assertIn('atlas.layout: The texture layout XML is malformed.', result.stderr)

    def test_omitted_native_icon_fails_independent_coverage(self):
        def remove_icon(output):
            metadata = output / 'assets.tsv'
            lines = metadata.read_text().splitlines()
            self.assertTrue(any(line.startswith('ICON\t1\t0\t') for line in lines))
            metadata.write_text(
                '\n'.join(line for line in lines if not line.startswith('ICON\t1\t0\t')) + '\n'
            )

        with self.assertRaisesRegex(AssertionError, r'Native icon coverage differs.*Status.*0'):
            self.audit_icons(remove_icon)

    def test_changed_native_pixels_fail(self):
        def change_pixel(output):
            atlas = output / 'atlas.rgba'
            data = bytearray(atlas.read_bytes())
            data[(2 * 16 * 164 + 2) * 4] ^= 1
            atlas.write_bytes(data)

        with self.assertRaisesRegex(AssertionError, 'Native DDS crop differs'):
            self.audit_icons(change_pixel)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-directory', type=Path, required=True)
    args, remaining = parser.parse_known_args()
    PROBES = args.build_directory.resolve()
    unittest.main(argv=[sys.argv[0], *remaining])
