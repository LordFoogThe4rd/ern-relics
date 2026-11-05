"""Audit the native active-regulation reader and validator against Python decoding.

Reads the supplied installation/mod file only. Never opens a save or game process.
Requires the same environment as tools.assets.extract.
"""

import argparse
import hashlib
import itertools
import json
import random
import struct
import subprocess
from collections import Counter
from pathlib import Path
from tempfile import TemporaryDirectory

from tools.assets import extract as assets

EMPTY = 0xFFFFFFFF


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def verify(game, source, probe, report):
    with TemporaryDirectory() as cache:
        raw = assets.Extractor(game, Path(cache)).regulation(source)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    effects = {}
    for eid, row in assets.checked_rows(raw['AttachEffectParam.param'], 3, 68):
        effects[eid] = (
            struct.unpack_from('<i', raw['AttachEffectParam.param'], row + 24)[0],
            struct.unpack_from('<i', raw['AttachEffectParam.param'], row + 52)[0],
            struct.unpack_from('<i', raw['AttachEffectParam.param'], row + 60)[0],
        )
    pools = {}
    for tid, row in assets.checked_rows(raw['AttachEffectTableParam.param'], 1, 12):
        _, eid, base, dlc = struct.unpack_from('<iiHh', raw['AttachEffectTableParam.param'], row)
        weight = pools.setdefault(tid, {}).setdefault(eid & EMPTY, [False, False])
        weight[0] |= base > 0
        weight[1] |= (dlc if dlc >= 0 else base) > 0
    items = {}
    for iid, row in assets.checked_rows(raw['EquipParamAntique.param'], 2, 48):
        b = raw['EquipParamAntique.param'][row : row + 48]
        if iid < 100 or b[0] & 1 or b[4] > 3:
            continue
        tables = struct.unpack_from('<3i', b, 16)
        active = [tid for tid in tables if tid != -1]
        if not active:
            continue
        choices = [
            {eid for eid, weight in pools.get(tid, {}).items() if eid != EMPTY and any(weight)}
            for tid in tables
        ]
        rolled = bool(b[6]) and all(
            len(choices[i]) > 1 for i, tid in enumerate(tables) if tid != -1
        )
        kind = (3 if b[7] & 1 else 0) if rolled else 1 if not b[6] else 2
        curse_tables = struct.unpack_from('<3i', b, 32)
        curses = [
            {eid for eid, weight in pools.get(curse, {}).items() if any(weight)}
            if b[7] & 1 and positive != -1
            else set()
            for positive, curse in zip(tables, curse_tables, strict=True)
        ]
        fixed = tuple(
            next(iter(choice)) if len(choice) == 1 else EMPTY for choice in choices + curses
        )
        items[iid] = (
            kind,
            b[4],
            struct.unpack_from('<I', b, 28)[0],
            tables,
            fixed,
            curse_tables,
            int(bool(b[7] & 1)),
        )
    deep_tables = sorted(
        {
            (positive, curse)
            for item in items.values()
            if item[0] == 3
            for positive, curse in zip(item[3], item[5], strict=True)
            if positive != -1
        }
    )

    expected = {f'SOURCE\t{digest}'}
    expected.update(
        '\t'.join(map(str, ['ITEM', iid, *item[:3], *item[3], *item[4], *item[5], item[6]]))
        for iid, item in items.items()
    )
    expected.update(
        '\t'.join(map(str, ['EFFECT', eid, *effect])) for eid, effect in effects.items()
    )
    expected.update(
        '\t'.join(map(str, ['POOL', tid, eid, int(weight[0]), int(weight[1])]))
        for tid, pool in pools.items()
        for eid, weight in pool.items()
    )
    command = [str(probe), str(source), str(game)]
    dumped = subprocess.run(
        [*command, '--dump'], capture_output=True, text=True, encoding='utf-8', check=True
    )
    actual = set(dumped.stdout.splitlines())
    require(
        actual == expected,
        f'Native parameters differ. Missing: {sorted(expected - actual)[:5]}; extra: {sorted(actual - expected)[:5]}',
    )

    def oracle(iid, dlc, values):
        item = items.get(iid)
        if (
            item is None
            or item[0] not in (0, 3)
            or any(tid != -1 and tid not in pools for tid in item[3])
        ):
            return 'Unknown'
        if item[0] == 3 and any(
            tid != -1 and tid not in pools for pair in deep_tables for tid in pair
        ):
            return 'Unknown'
        if sum(eid != EMPTY for eid in values[:3]) != sum(tid != -1 for tid in item[3]):
            return 'Impossible'
        seen, groups, exclusions = set(), set(), set()
        for eid in (eid for eid in values if eid != EMPTY):
            effect = effects.get(eid)
            if effect is None:
                return 'Unknown'
            if (
                eid in seen
                or (effect[0] >= 0 and effect[0] in groups)
                or (effect[2] >= 0 and effect[2] in exclusions)
            ):
                return 'Impossible'
            seen.add(eid)
            groups.add(effect[0])
            exclusions.add(effect[2])

        # Assign available effects to lottery slots by backtracking, independently
        # of the production validator's sorted permutation enumeration.
        def allowed(table, eid, present):
            return eid == EMPTY if table == -1 else pools[table].get(eid, [False, False])[present]

        def possible(present, slot=0, remaining=values[:3]):
            if item[0] == 3:
                return all(
                    curse == EMPTY
                    if effect == EMPTY
                    else any(
                        allowed(pos, effect, present) and allowed(neg, curse, present)
                        for pos, neg in deep_tables
                    )
                    for effect, curse in zip(values[:3], values[3:], strict=True)
                )
            if any(eid != EMPTY for eid in values[3:]):
                return False
            if slot == 3:
                return True
            table = item[3][slot]
            for index, eid in enumerate(remaining):
                if allowed(table, eid, present) and possible(
                    present, slot + 1, remaining[:index] + remaining[index + 1 :]
                ):
                    return True
            return False

        base = dlc != 1 and possible(0)
        present = dlc != 0 and possible(1)
        if dlc == 2 and base != present:
            return 'Unknown'
        if not base and not present:
            return 'Impossible'
        order = sorted(
            range(3),
            key=lambda i: (
                values[i] == EMPTY,
                effects[values[i]][1] if values[i] != EMPTY else 0,
                values[i],
            ),
        )
        ordered = [values[i] for i in order] + [values[i + 3] for i in order]
        return 'Obtainable' + ''.join(f'\t{eid}' for eid in ordered)

    randomizer = random.Random(20261031)
    cases = []
    for iid, item in sorted(items.items()):
        if item[0] not in (0, 3):
            cases.append((iid, 2, (7000400, 7000602, 7001802, EMPTY, EMPTY, EMPTY)))
            continue
        for dlc in range(3):

            def choices_for(tid, dlc=dlc):
                return (
                    sorted(eid for eid, weight in pools.get(tid, {}).items() if weight[dlc == 1])
                    if tid != -1
                    else [EMPTY]
                )

            templates = (
                [randomizer.choice(deep_tables) if tid != -1 else (-1, -1) for tid in item[3]]
                if item[0] == 3
                else list(zip(item[3], item[5], strict=True))
            )
            choices = [choices_for(tid) for pair in templates for tid in pair]
            if not all(choices):
                continue
            for _ in range(3):
                paired = [randomizer.choice(choice) for choice in choices]
                values = tuple(paired[::2] + paired[1::2])
                cases.append((iid, dlc, values))
            cases.extend(
                (iid, dlc, tuple(values[i] for i in order) + tuple(values[i + 3] for i in order))
                for order in itertools.permutations(range(3))
            )
            cases.append((iid, dlc, (values[0], values[0], values[2], *values[3:])))
            cases.append((iid, dlc, (0x7FFFFFF0, *values[1:])))
            cases.append((iid, dlc, (values[0], values[1], EMPTY, *values[3:])))
            if item[0] == 3:
                cases.append((iid, dlc, (*values[:3], EMPTY, EMPTY, EMPTY)))
                cases.append((iid, dlc, (*values[:3], values[5], values[4], values[3])))
                cases.append((iid, dlc, (*values[:3], values[0], *values[4:])))
    for iid in (1003002, 1008002):
        cases.append((iid, 1, (7020000, 6630200, 7005600, EMPTY, EMPTY, EMPTY)))
    input_text = ''.join(
        ' '.join(map(str, [iid, dlc, *values])) + '\n' for iid, dlc, values in cases
    )
    validated = subprocess.run(
        command, input=input_text, capture_output=True, text=True, encoding='utf-8', check=True
    )
    results = validated.stdout.splitlines()
    require(len(results) == len(cases), 'Native validator returned the wrong number of cases')
    for case, result in zip(cases, results, strict=True):
        require(
            result == oracle(*case),
            f'Validation disagreement for {case}: {result} vs {oracle(*case)}',
        )
    summary = {
        'regulation': str(source),
        'regulation_sha256': digest,
        'relic_definitions': len(items),
        'effects': len(effects),
        'pools': len(pools),
        'compared_records': len(expected),
        'validation_cases': len(cases),
        'results': dict(Counter(line.split('\t')[0] for line in results)),
        'deep_validation_cases': sum(items.get(iid, (None,))[0] == 3 for iid, _, _ in cases),
        'native_diagnostics': validated.stderr.splitlines(),
    }
    if report:
        report.parent.mkdir(parents=True, exist_ok=True)
        report.write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-dir', required=True, type=Path)
    parser.add_argument('--regulation', type=Path)
    parser.add_argument('--probe', type=Path, required=True)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    verify(
        args.game_dir.resolve(),
        (args.regulation or args.game_dir / 'regulation.bin').resolve(),
        args.probe.resolve(),
        args.report.resolve() if args.report else None,
    )
