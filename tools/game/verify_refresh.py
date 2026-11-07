"""Compare read-only probe captures from before Apply, after Apply, and after Undo.

Use a test edit that changes an active special-effect ID. Add --armaments for a
starting-weapon affinity edit and capture with probe --armaments. Effect IDs alone
do not prove that the weapon changed. This never opens a game process or save.
"""

import argparse
import ast
import json
import re
from collections import Counter
from pathlib import Path
from typing import NamedTuple


class Snapshot(NamedTuple):
    slots: tuple[str, ...]
    handles: tuple[int, ...]
    active: tuple[int, ...]
    armaments: tuple[tuple[int, ...] | None, ...] | None


def snapshot(path):
    text = path.read_text(encoding='utf-8-sig')
    slots = tuple(re.findall(r'^(?:Normal|Deep) relic [1-3]:[^\n]+', text, re.MULTILINE))
    labels = tuple(f'{kind} relic {index}:' for kind in ('Normal', 'Deep') for index in range(1, 4))
    handles = re.search(r'cached handles=(\([^\n]*\))', text)
    active = re.search(r'Active special effects: (\d+), IDs=(\[[^\n]*\])', text)
    if (
        len(slots) != 6
        or not all(slot.startswith(label) for slot, label in zip(slots, labels, strict=True))
        or not handles
        or not active
    ):
        raise ValueError(f'{path}: expected a complete --inspect --effects capture')
    handle_ids, active_ids = ast.literal_eval(handles[1]), ast.literal_eval(active[2])
    if (
        len(handle_ids) not in range(6, 128, 6)
        or len(active_ids) != int(active[1])
        or not all(type(value) is int for value in (*handle_ids, *active_ids))
    ):
        raise ValueError(f'{path}: invalid native handles or active-effect list')
    weapons = re.search(r'^Equipped armaments: (\([^\n]*\))', text, re.MULTILINE)
    armaments = ast.literal_eval(weapons[1]) if weapons else None
    if armaments is not None and (
        len(armaments) != 6
        or not all(
            weapon is None
            or isinstance(weapon, tuple)
            and len(weapon) == 5
            and all(type(value) is int for value in weapon)
            for weapon in armaments
        )
    ):
        raise ValueError(f'{path}: invalid equipped armament records')
    return Snapshot(slots, tuple(handle_ids), tuple(sorted(active_ids)), armaments)


def verify(before, applied, undone, *, armaments=False):
    if before.handles != applied.handles or before.handles != undone.handles:
        raise ValueError('Equipment handles changed; repeat without re-equipping relics')
    changed = [
        index + 1
        for index, (old, new) in enumerate(zip(before.slots, applied.slots, strict=True))
        if old != new
    ]
    if not changed:
        raise ValueError('Apply did not change the stored relic effects')
    old_active, new_active = Counter(before.active), Counter(applied.active)
    if old_active == new_active:
        raise ValueError('No active special-effect IDs changed; use a measurable test effect')
    if before != undone:
        raise ValueError('Undo did not restore the full stored and active baseline')
    result = {
        'changed_slots': changed,
        'removed_active_ids': sorted((old_active - new_active).elements()),
        'added_active_ids': sorted((new_active - old_active).elements()),
        'equipped_handles_unchanged': True,
        'undo_restored_inventory': True,
        'undo_restored_active_effects': True,
    }
    if armaments:
        if any(state.armaments is None for state in (before, applied, undone)):
            raise ValueError('Armament verification requires three probe --armaments captures')
        if before.armaments == applied.armaments:
            raise ValueError('The active effects changed, but equipped armaments did not refresh')
        if any(
            old is None or new is None or old[:2] != new[:2]
            for old, new in zip(before.armaments, applied.armaments, strict=True)
            if old != new
        ):
            raise ValueError('Weapon identity changed; repeat with an affinity-only edit')
        result['armament_modifiers_changed'] = True
        result['undo_restored_armaments'] = True
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('applied', type=Path)
    parser.add_argument('undone', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument(
        '--armaments',
        action='store_true',
        help='Require a real weapon-modifier change and its restoration',
    )
    args = parser.parse_args()
    try:
        result = verify(
            *(snapshot(path) for path in (args.before, args.applied, args.undone)),
            armaments=args.armaments,
        )
        output = json.dumps(result, indent=2) + '\n'
        if args.report:
            args.report.write_text(output, encoding='utf-8')
        print(output, end='')
    except (OSError, ValueError, SyntaxError, TypeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
