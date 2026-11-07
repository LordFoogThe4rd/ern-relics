"""Read-only signature diagnostics for an already-running Nightreign process."""

import argparse
import ctypes as c
import re
import struct
from ctypes import wintypes as w


class ModuleEntry(c.Structure):
    _fields_ = [
        ('size', w.DWORD),
        ('module_id', w.DWORD),
        ('pid', w.DWORD),
        ('global_usage', w.DWORD),
        ('process_usage', w.DWORD),
        ('base', c.c_void_p),
        ('image_size', w.DWORD),
        ('module', w.HMODULE),
        ('name', w.WCHAR * 256),
        ('path', w.WCHAR * 260),
    ]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument(
        '--inspect',
        action='store_true',
        help='Read all six equipped relic records, including paired curses',
    )
    parser.add_argument(
        '--records',
        action='store_true',
        help='Include the first 96 bytes of each equipped item record',
    )
    parser.add_argument(
        '--location',
        action='store_true',
        help='Read live hub state and player presence without changing the game',
    )
    parser.add_argument(
        '--effects',
        action='store_true',
        help='Read the App 1.03.3 relic controller and active special-effect list',
    )
    parser.add_argument(
        '--armaments',
        action='store_true',
        help='Read the six equipped weapon records, including stored affinity modifiers',
    )
    parser.add_argument(
        '--callback',
        action='store_true',
        help='Read the App 1.03.3 equipment callback and original instructions',
    )
    args = parser.parse_args()
    kernel = c.WinDLL('kernel32', use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD, w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Module32FirstW.argtypes = [w.HANDLE, c.POINTER(ModuleEntry)]
    kernel.Module32NextW.argtypes = [w.HANDLE, c.POINTER(ModuleEntry)]
    kernel.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    kernel.OpenProcess.restype = w.HANDLE
    kernel.ReadProcessMemory.argtypes = [
        w.HANDLE,
        c.c_void_p,
        c.c_void_p,
        c.c_size_t,
        c.POINTER(c.c_size_t),
    ]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(0x18, args.pid)
    if snapshot == c.c_void_p(-1).value:
        raise c.WinError(c.get_last_error())
    entry = ModuleEntry()
    entry.size = c.sizeof(entry)
    game_module = None
    modules = []
    found = kernel.Module32FirstW(snapshot, c.byref(entry))
    while found:
        modules.append((entry.name, entry.base, entry.image_size))
        if entry.name.casefold() == 'nightreign.exe':
            game_module = ModuleEntry.from_buffer_copy(entry)
        found = kernel.Module32NextW(snapshot, c.byref(entry))
    kernel.CloseHandle(snapshot)
    if game_module is None:
        raise RuntimeError('The supplied PID is not a Nightreign process')
    entry = game_module
    process = kernel.OpenProcess(0x1010, False, args.pid)
    if not process:
        raise c.WinError(c.get_last_error())

    def read(address, size):
        buffer = c.create_string_buffer(size)
        copied = c.c_size_t()
        if (
            not kernel.ReadProcessMemory(process, address, buffer, size, c.byref(copied))
            or copied.value != size
        ):
            raise c.WinError(c.get_last_error())
        return buffer.raw

    try:
        if args.callback:
            original = entry.base + 0x675D20
            slot = entry.base + 0x2C30880
            target = struct.unpack('<Q', read(slot, 8))[0]
            owner = next(
                (name for name, base, size in modules if base <= target < base + size), 'unknown'
            )
            instructions = read(original, 16)
            expected = bytes.fromhex('48 8b c4 48 89 58 18 55 56 57 41 54 41 55 41 56')
            print(
                f'Equipment update: address={original:#x}, instructions={instructions.hex(" ")}, unchanged={instructions == expected}'
            )
            print(
                f'Equipment callback: slot={slot:#x}, target={target:#x}, module={owner}, original_target={target == original}'
            )
        roots = {}
        headers = read(entry.base, 0x1000)
        nt = struct.unpack_from('<I', headers, 0x3C)[0]
        sections = struct.unpack_from('<H', headers, nt + 6)[0]
        optional_size = struct.unpack_from('<H', headers, nt + 20)[0]
        section_table = nt + 24 + optional_size
        for index in range(sections):
            offset = section_table + index * 40
            if headers[offset : offset + 8].rstrip(b'\0') != b'.text':
                continue
            size, rva = struct.unpack_from('<II', headers, offset + 8)
            code = read(entry.base + rva, size)
            print(f'Nightreign PID {args.pid}, .text RVA {rva:#x}, size {size:#x}')
            patterns = [
                ('CSGaitem', '8B 03 89 44 24 40 48 8B 0D ?? ?? ?? ?? 48 85 C9', 9),
                ('GameDataMan', '48 8B 05 ?? ?? ?? ?? 48 8B 70 08', 3),
                ('WorldChrMan', '48 8B 05 ?? ?? ?? ?? 0F 28 F1 48 85 C0', 3),
            ]
            for name, text, displacement_offset in patterns:
                regex = b''.join(
                    b'.' if part == '??' else re.escape(bytes.fromhex(part))
                    for part in text.split()
                )
                matches = list(re.finditer(regex, code, re.DOTALL))
                print(f'{name}: {len(matches)} instruction matches')
                globals_found = set()
                for match in matches[:32]:
                    displacement = struct.unpack_from(
                        '<i', code, match.start() + displacement_offset
                    )[0]
                    target_rva = rva + match.start() + displacement_offset + 4 + displacement
                    globals_found.add(target_rva)
                    try:
                        pointer = struct.unpack('<Q', read(entry.base + target_rva, 8))[0]
                        detail = f'pointer {pointer:#x}'
                    except OSError:
                        detail = 'unreadable pointer'
                    print(
                        f'  instruction RVA {rva + match.start():#x}, global RVA {target_rva:#x}, {detail}'
                    )
                print(f'  distinct globals: {len(globals_found)}')
                if len(globals_found) == 1:
                    roots[name] = entry.base + next(iter(globals_found))
        if args.location:
            try:
                game = struct.unpack('<Q', read(roots['GameDataMan'], 8))[0]
                world = struct.unpack('<Q', read(roots['WorldChrMan'], 8))[0]
                world_type = read(game + 0xD8, 1)[0]
                live_player = struct.unpack('<Q', read(world + 0x174E8, 8))[0]
                if live_player:
                    read(live_player + 8, 8)
                print(
                    f'Live location: world_type={world_type}, live_player={live_player:#x}, Roundtable editing allowed={world_type == 0 and live_player != 0}'
                )
            except (KeyError, OSError):
                print('Live location: unavailable; Roundtable editing allowed=False')
        if args.inspect and 'CSGaitem' in roots and 'GameDataMan' in roots:
            items = struct.unpack('<Q', read(roots['CSGaitem'], 8))[0]
            game = struct.unpack('<Q', read(roots['GameDataMan'], 8))[0]
            player = struct.unpack('<Q', read(game + 8, 8))[0]
            print(f'Player data: {player:#x}')
            if player:
                for index in range(6):
                    label = f'{"Normal" if index < 3 else "Deep"} relic {index % 3 + 1}'
                    handle = struct.unpack('<I', read(player + 0x2F4 + index * 4, 4))[0]
                    slot = handle & 0xFFFF
                    if handle == 0 or slot == 0xFFFF:
                        print(f'{label}: empty')
                        continue
                    record = struct.unpack('<Q', read(items + 8 + slot * 8, 8))[0]
                    effects = struct.unpack('<III', read(record + 0x18, 12))
                    curses = struct.unpack('<III', read(record + 0x40, 12))
                    print(f'{label}: inventory index {slot}, effects {effects}, curses {curses}')
                    if args.records:
                        record_bytes = read(record, 96)
                        for offset in range(0, 96, 16):
                            values = struct.unpack_from('<4I', record_bytes, offset)
                            print(
                                f'  +{offset:02x}: ' + ' '.join(f'{value:08x}' for value in values)
                            )
        if args.effects:

            def pointer(address):
                return struct.unpack('<Q', read(address, 8))[0]

            world = pointer(roots['WorldChrMan'])
            player = pointer(world + 0x174E8)
            controller = pointer(player + 0x650)
            if pointer(controller) != entry.base + 0x2C1AB28 or pointer(controller + 8) != player:
                raise RuntimeError(
                    'The live relic controller does not match the supported App 1.03.3 layout'
                )
            count = struct.unpack('<I', read(controller + 0x20, 4))[0]
            if count not in range(6, 128, 6):
                raise RuntimeError(f'Unexpected native relic-controller entry count: {count}')
            cached = struct.unpack(f'<{count}I', read(pointer(controller + 0x18), count * 4))
            print(
                f'Native relic controller: {controller:#x}, player={player:#x}, entries={count}, cached handles={cached}'
            )
            active = pointer(controller + 0x10)
            node = pointer(active + 8)
            seen, ids = set(), []
            while node:
                if node in seen or len(seen) >= 2048:
                    raise RuntimeError(
                        'The active effect list changed or exceeds the supported diagnostic limit'
                    )
                seen.add(node)
                ids.append(struct.unpack('<i', read(node + 8, 4))[0])
                node = pointer(node + 0x38)
            print(f'Active special effects: {len(ids)}, IDs={sorted(ids)}')
        if args.armaments:

            def pointer(address):
                return struct.unpack('<Q', read(address, 8))[0]

            game, items = pointer(roots['GameDataMan']), pointer(roots['CSGaitem'])
            data = pointer(game + 8)
            world = pointer(roots['WorldChrMan'])
            player = pointer(world + 0x174E8)
            if not player or pointer(player + 0x5A0) != data:
                raise RuntimeError('The local character does not own the current player data')
            handles = struct.unpack('<6I', read(data + 0x28C, 24))
            armaments = []
            for handle in handles:
                if handle == 0 or handle & 0xFFFF == 0xFFFF:
                    armaments.append(None)
                    continue
                record = pointer(items + 8 + (handle & 0xFFFF) * 8)
                if struct.unpack('<I', read(record + 8, 4))[0] != handle:
                    raise RuntimeError('An equipped weapon handle changed during the capture')
                values = tuple(
                    struct.unpack('<i', read(record + offset, 4))[0]
                    for offset in (0x0C, 0x1C, 0x2C, 0x40)
                )
                armaments.append((handle, *values))
            if (
                handles != struct.unpack('<6I', read(data + 0x28C, 24))
                or pointer(game + 8) != data
                or pointer(world + 0x174E8) != player
                or pointer(player + 0x5A0) != data
            ):
                raise RuntimeError('Equipped weapons changed during the capture')
            print('Armament fields: (handle, weapon ID, skill ID, spell ID, affinity effect ID)')
            print(f'Equipped armaments: {tuple(armaments)}')
    finally:
        kernel.CloseHandle(process)


if __name__ == '__main__':
    main()
