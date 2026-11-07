"""Prevent a changed effect list from passing as a refreshed weapon."""

import tempfile
import unittest
from pathlib import Path

from tools.game.verify_refresh import snapshot, verify


def capture(effect, affinity, entries):
    slots = [f'Normal relic 1: inventory index 1, effects ({effect}, 2, 3), curses (-1, -1, -1)']
    slots.extend(
        f'{kind} relic {index}: empty'
        for kind, indices in (('Normal', (2, 3)), ('Deep', (1, 2, 3)))
        for index in indices
    )
    handles = (1,) + (0,) * (entries - 1)
    weapons = ((10, 20, 30, -1, affinity),) + (None,) * 5
    return '\n'.join(
        [
            *slots,
            f'cached handles={handles}',
            f'Active special effects: 1, IDs=[{effect}]',
            f'Equipped armaments: {weapons}',
        ]
    )


class WeaponRefreshVerification(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='refresh-test-')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name).resolve()

    def state(self, name, effect, affinity, entries=24):
        path = self.root / name
        path.write_text(capture(effect, affinity, entries), encoding='utf-8')
        return snapshot(path)

    def test_changed_effect_with_stale_weapon_fails(self):
        before = self.state('before.txt', 100, 100)
        applied = self.state('applied.txt', 200, 100)
        with self.assertRaisesRegex(ValueError, 'armaments did not refresh'):
            verify(before, applied, before, armaments=True)

    def test_undo_must_restore_weapon_too(self):
        before = self.state('before.txt', 100, 100)
        applied = self.state('applied.txt', 200, 200)
        undone = self.state('undone.txt', 100, 200)
        with self.assertRaisesRegex(ValueError, 'Undo did not restore'):
            verify(before, applied, undone, armaments=True)

    def test_affinity_replacement_and_removal(self):
        before = self.state('before.txt', 100, 100)
        for affinity in (200, -1):
            with self.subTest(affinity=affinity):
                applied = self.state('applied.txt', 200, affinity)
                result = verify(before, applied, before, armaments=True)
                self.assertTrue(result['armament_modifiers_changed'])
                self.assertTrue(result['undo_restored_armaments'])

    def test_larger_cache(self):
        before = self.state('before.txt', 100, 100, entries=48)
        applied = self.state('applied.txt', 200, 200, entries=48)
        self.assertTrue(verify(before, applied, before)['undo_restored_active_effects'])


if __name__ == '__main__':
    unittest.main()
