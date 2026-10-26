"""Seek pool, challenge filter, finger titles, rematch limit, stopping, and JSON settings."""
import asyncio
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest.mock import patch

import chess.engine

from maxima_runner import fics, learning
from test_fics import fake_server, guest_login, options

board_as_black = ("<12> rnbqkbnr pppppppp -------- -------- -------- -------- PPPPPPPP RNBQKBNR W -1 1 1 1 1 0 7 "
                  "GuestOPPO GuestTEST -1 3 0 39 39 180000 180000 1 none (0:00.000) none 0 0 0")
creating = "Creating: GuestOPPO (++++) GuestTEST (++++) unrated blitz 3 0"
game_end = "{Game 7 (GuestOPPO vs. GuestTEST) GuestOPPO resigns} 0-1"


class fake_connection:
    def __init__(self):
        self.sent = []

    async def send(self, line, secret=False):
        self.sent.append(line)


def player(**policy):
    lines = []
    result = fics.engine_player(None, lines.append, policy=fics.play_policy(**policy))
    result.connection = fake_connection()
    result.lines = lines
    return result


async def play_one_game(subject):
    await subject.handle(creating)
    await subject.handle(board_as_black)
    subject.connection.sent.clear()
    await subject.handle(game_end)


class policy_tests(unittest.IsolatedAsyncioTestCase):
    async def test_login_posts_unrated_seeks_from_the_pool(self):
        subject = player(seeks=["3 0", "5 2 r", "1 0 unrated"], seeks_at_once=3)
        await subject.on_login("GuestTEST")
        self.assertEqual(subject.connection.sent[0], "unseek")
        self.assertCountEqual(subject.connection.sent[1:], ["seek 3 0 u", "seek 5 2 u", "seek 1 0 u"])
        registered = player(seeks=["3 0"], guest=False)
        await registered.on_login("Maxima")
        self.assertEqual(registered.connection.sent[-2:], ["unseek", "seek 3 0"])

    async def test_registered_login_sets_finger_notes(self):
        subject = player(seeks=["3 0"], guest=False, owner=["hof", "hajewiet"], notes=["Maxima plays unrated tests"])
        protocol = type("protocol", (), {"id": {"name": "Maxima 2.1"}, "config": {"Hash": 256},
                                         "options": {"Threads": types.SimpleNamespace(default=1)}})
        subject.session = types.SimpleNamespace(protocol=protocol)
        await subject.on_login("Maxima")
        self.assertEqual(subject.connection.sent[:2], ["set 1 Maxima 2.1 on MaxDrone",
                                                       "set 2 Owner: hof and hajewiet"])
        self.assertRegex(subject.connection.sent[2], r"^set 3 Hardware: .+, 1 thread, Hash 256 MB, \w+$")
        self.assertEqual(subject.connection.sent[3:], ["set 4 Maxima plays unrated tests", "unseek", "seek 3 0"])
        guest = player(seeks=["3 0"])
        await guest.on_login("GuestTEST")
        self.assertEqual(guest.connection.sent, ["unseek", "seek 3 0 u"])

    def test_engine_name_is_shortened_to_version(self):
        self.assertEqual(fics.short_engine_name("Maxima 2.0.0-feat/nnue-bootstrap-8778e07-Release"), "Maxima 2.0.0")
        self.assertEqual(fics.short_engine_name("Maxima 2.1"), "Maxima 2.1")
        self.assertEqual(fics.short_engine_name("Stockfish 17.1"), "Stockfish 17.1")
        self.assertEqual(fics.short_engine_name("Crafty"), "Crafty")
        self.assertEqual(fics.short_engine_name(None), "UCI engine")

    def test_cpu_model_is_shortened(self):
        model = fics.cpu_model()
        self.assertNotRegex(model, r"\(R\)|\(TM\)|@")

    async def test_no_rematch_by_default(self):
        subject = player(seeks=["3 0"])
        await subject.on_login("GuestTEST")
        await play_one_game(subject)
        self.assertEqual(subject.connection.sent, ["unseek", "seek 3 0 u"])

    async def test_challenges_are_filtered_by_variant_and_state(self):
        subject = player()
        await subject.on_login("GuestTEST")
        for line, reply in (
                ("Challenge: GuestAAAA (++++) GuestTEST (++++) unrated blitz 3 0.", "accept GuestAAAA"),
                ("Challenge: Foo (1500) [white] GuestTEST (++++) unrated lightning 1 0.", "accept Foo"),
                ("Challenge: GuestBBBB (++++) GuestTEST (++++) unrated wild/fr 3 0.", "decline GuestBBBB"),
                ("Challenge: GuestCCCC (++++) GuestTEST (++++) unrated crazyhouse 2 12.", "decline GuestCCCC"),
                ("Challenge: GuestDDDD (++++) GuestTEST (++++) unrated untimed 0 0.", "decline GuestDDDD")):
            await subject.handle(line)
            self.assertEqual(subject.connection.sent[-1], reply)
        await subject.handle(board_as_black)
        await subject.handle("Challenge: GuestEEEE (++++) GuestTEST (++++) unrated blitz 3 0.")
        self.assertEqual(subject.connection.sent[-1], "decline GuestEEEE")
        closed = player(accept=False)
        await closed.handle("Challenge: GuestAAAA (++++) GuestTEST (++++) unrated blitz 3 0.")
        self.assertEqual(closed.connection.sent, ["decline GuestAAAA"])

    async def test_creating_fingers_opponent_and_reads_titles(self):
        subject = player()
        await subject.on_login("GuestTEST")
        await subject.handle("Creating: Stockfish (2950) GuestTEST (++++) unrated blitz 3 0")
        self.assertEqual(subject.connection.sent[-1], "finger Stockfish")
        self.assertFalse(subject.opponent_ready.is_set())
        await subject.handle("Finger of Stockfish(C)(TD):")
        self.assertEqual(subject.opponent, chess.engine.Opponent("Stockfish", None, 2950, True))
        self.assertTrue(subject.opponent_ready.is_set())
        await subject.handle("Creating: GuestTEST (++++) Magnus (2880) unrated blitz 3 0")
        await subject.handle("Finger of Magnus(GM):")
        self.assertEqual(subject.opponent, chess.engine.Opponent("Magnus", "GM", 2880, False))

    async def test_unsupported_game_is_aborted(self):
        subject = player()
        await subject.on_login("GuestTEST")
        await subject.handle("Creating: GuestOPPO (++++) GuestTEST (++++) unrated losers 3 0")
        self.assertEqual(subject.connection.sent[-1], "abort")

    async def test_rematches_are_limited_per_opponent(self):
        subject = player(seeks=["3 0"], rematches=2)
        await subject.on_login("GuestTEST")
        sent = []
        for _ in range(3):
            await play_one_game(subject)
            sent.append(list(subject.connection.sent))
        self.assertEqual(sent[0], ["rematch", "unseek", "seek 3 0 u"])
        self.assertEqual(sent[1], ["rematch", "unseek", "seek 3 0 u"])
        self.assertEqual(sent[2], ["unseek", "seek 3 0 u"])
        self.assertIsNone(subject.opponent)

    async def test_stops_after_game_limit_or_quit_request(self):
        subject = player(seeks=["3 0"], games=1)
        await subject.on_login("GuestTEST")
        await play_one_game(subject)
        self.assertTrue(subject.finished)
        self.assertEqual(subject.connection.sent, ["unseek"])
        waiting = player(seeks=["3 0"])
        await waiting.on_login("GuestTEST")
        await waiting.handle(board_as_black)
        await waiting.stop()
        self.assertFalse(waiting.finished)
        await waiting.handle(game_end)
        self.assertTrue(waiting.finished)

    async def test_client_quits_by_itself_after_game_limit(self):
        async def script(number, writer, read_line):
            await guest_login(writer, read_line)
            self.assertEqual(await read_line(), "unseek")
            self.assertEqual(await read_line(), "seek 3 0 u")
            for line in (creating, board_as_black, game_end):
                writer.write(f"\n\r{line}\n\rfics% ".encode())
            self.assertEqual(await read_line(), "finger GuestOPPO")
            self.assertEqual(await read_line(), "unseek")
            self.assertEqual(await read_line(), "quit")

        subject = player(seeks=["3 0"], games=1)
        async with fake_server(script) as server:
            result = await asyncio.wait_for(
                fics.run_client(options(server.port), asyncio.Queue(), subject.lines.append, player=subject), 10)
        self.assertEqual(result, 0)


class settings_tests(unittest.TestCase):
    def test_config_discovery_precedence_and_explicit_override(self):
        with tempfile.TemporaryDirectory() as directory:
            roots = [Path(directory) / name for name in ('cwd', 'data', 'project')]
            for root in roots:
                root.mkdir()
            with patch.object(fics.Path, 'cwd', return_value=roots[0]), \
                    patch.object(fics, 'data_root', return_value=roots[1]), \
                    patch.object(fics, 'project_root', return_value=roots[2]):
                self.assertIsNone(fics.config_path())
                for root in reversed(roots):
                    path = root / '.fics.json'
                    path.write_text(json.dumps({'seeks': ['5 1'], 'owner': ['hof'], 'notes': ['Hello']}))
                    self.assertEqual(fics.config_path(), path)
                explicit = roots[2] / 'other.json'
                self.assertEqual(fics.config_path(explicit), explicit)
                with self.assertRaises(FileNotFoundError):
                    fics.load_settings(fics.config_path(explicit))
                (roots[0] / '.fics.json').write_text('invalid json')
                with self.assertRaises(ValueError):
                    fics.load_settings(fics.config_path())

    def write(self, value):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "fics.json"
        path.write_text(json.dumps(value))
        return str(path)

    def test_wild_config_reaches_startup_and_posts_each_variant(self):
        from maxima_runner.fics_cli import parser
        specs = ['3 0 ' + kind for kind in ('w2', 'w3', 'w4', 'w5', 'w8', 'w8a', 'losers')]
        arguments = parser()
        options = arguments.parse_args(['--config', self.write({
            'seeks': specs, 'seeks_at_once': 7})])
        policies = []
        factory = fics.play_policy

        def capture(*args, **kwargs):
            subject = factory(*args, **kwargs)
            policies.append(subject)
            return subject

        def without_connecting(coroutine):
            coroutine.close()
            return 0

        with patch.object(fics, 'play_policy', side_effect=capture), \
                patch.object(fics.asyncio, 'run', side_effect=without_connecting), patch('builtins.print'):
            self.assertEqual(fics.run(options, arguments), 0)
        self.assertCountEqual(policies[0].seek_commands(),
                              ['seek ' + spec + ' u' for spec in specs])
        self.assertTrue(policies[0].losers)

    def test_config_rejects_unsupported_or_combined_variants(self):
        from maxima_runner.fics_cli import parser
        for pool, spec in [('seeks', '3 0 ' + kind) for kind in
                           ('w0', 'w1', 'w17', 'w8b', 'suicide', 'atomic', 'losers w2', 'w2 w3')] + [
                               ('losers_seeks', '3 0 w2')]:
            with self.subTest(pool=pool, spec=spec):
                arguments = parser()
                options = arguments.parse_args(['--config', self.write({pool: [spec]})])
                with patch('sys.stderr'), self.assertRaises(SystemExit) as error:
                    fics.run(options, arguments)
                self.assertEqual(error.exception.code, 2)

    def test_merges_file_over_defaults(self):
        settings = fics.load_settings(self.write({"seeks": ["1 0"], "games": 5, "options": {"Hash": 256}}))
        self.assertEqual((settings["seeks"], settings["games"], settings["options"]), (["1 0"], 5, {"Hash": "256"}))
        self.assertEqual(settings["host"], "freechess.org")
        self.assertEqual(fics.load_settings(None), fics.default_settings)
        profiles = fics.load_settings(self.write({"layer_profiles": {"weaker": {"fire": 35}}}))["layer_profiles"]
        self.assertEqual(profiles["weaker"]["fire"], 35)
        self.assertEqual(profiles["equal"], learning.layer_profiles["equal"])
        books = fics.style_books(fics.load_settings(self.write({"style_books": {"fire": "ph-gambitbook.bin",
                                                                                "draw": "/books/draw.bin"}}))
                                 ["style_books"])
        self.assertEqual((books["fire"].name, str(books["draw"]), books["experiment"].name),
                         ("ph-gambitbook.bin", "/books/draw.bin", "experiment.bin"))

    def test_rejects_unknown_keys_and_wrong_types(self):
        for value, message in (({"seek": ["3 0"]}, "unknown"), ({"games": "5"}, "games must be int"),
                               ({"rematches": True}, "rematches must be int"), ({"seeks": [3]}, "strings"), ({"notes": ["x"] * 8}, "at most 7"), ({"owner": 5}, "owner must be str or list"),
                               ({"owner": ["hof", 5]}, "list of handles"),
                               ({"layer_profiles": {"hot": {}}}, "has profiles"),
                               ({"style_books": {"gambit": "x.bin"}}, "style_books maps"),
                               ({"layer_profiles": {"weaker": {"style": 3}}}, "takes non-negative"),
                               ({"layer_profiles": {"weaker": {"book": -1}}}, "takes non-negative"),
                               ([], "JSON object")):
            with self.assertRaisesRegex(ValueError, message):
                fics.load_settings(self.write(value))


if __name__ == "__main__":
    unittest.main()


class remote_pool_tests(unittest.IsolatedAsyncioTestCase):
    async def test_owner_replaces_mixed_pool_and_it_survives_game_end(self):
        subject = player(owner=['HoF'], seeks=['9 0'])
        await subject.on_login('GuestTEST')
        subject.connection.sent.clear()
        await subject.handle('hof tells you: seek-pool 3 0, 5 1 losers, 4 0 losers, 1 0 w2')
        self.assertEqual(subject.policy.seek_pool, ['3 0', '5 1 losers', '4 0 losers', '1 0 w2'])
        self.assertTrue(subject.policy.losers)
        self.assertIn('set f3 losers', subject.connection.sent)
        self.assertEqual(sum(line.startswith('seek ') for line in subject.connection.sent), 2)
        await play_one_game(subject)
        self.assertEqual(sum(line.startswith('seek ') for line in subject.connection.sent), 2)
        self.assertNotIn('seek 9 0 u', subject.connection.sent)

    async def test_key_and_rejected_pool_leave_policy_intact(self):
        subject = player(follow_key='secret', seeks=['3 0'])
        await subject.handle('stranger tells you: seek-pool 1 0 losers')
        self.assertIsNone(subject.policy.seek_pool)
        await subject.handle('stranger tells you: secret seek-pool 5 1 losers')
        self.assertEqual(subject.policy.seek_pool, ['5 1 losers'])
        for spec in ('3 0,', '0 0', '3 0 suicide', '3 0 w0', '3 0 losers w2', '3 0; quit'):
            await subject.handle(f'stranger tells you: secret seek-pool {spec}')
            self.assertEqual(subject.policy.seek_pool, ['5 1 losers'])

    async def test_active_game_updates_only_future_seeks(self):
        subject = player(owner='hof')
        subject.game = 7
        await subject.handle('hof tells you: seek-pool 3 0, 5 1')
        self.assertEqual(subject.policy.seek_pool, ['3 0', '5 1'])
        self.assertFalse(any(line == 'unseek' or line.startswith('seek ') for line in subject.connection.sent))

    async def test_supported_wild_challenges(self):
        subject = player()
        for kind in fics.wild_types:
            await subject.handle(f'Challenge: Beta (1500) Alpha (1500) unrated {kind} 3 0.')
            self.assertEqual(subject.connection.sent[-1], 'accept Beta')
        await subject.handle('Challenge: Beta (1500) Alpha (1500) unrated wild/0 3 0.')
        self.assertEqual(subject.connection.sent[-1], 'decline Beta')


class weighted_seek_tests(unittest.IsolatedAsyncioTestCase):
    def test_remaining_shares_and_hundred_percent_override(self):
        self.assertEqual(fics.seek_weights(['5 0 50%', '3 0 w2', '3 0 losers']),
                         [('5 0', 50), ('3 0 w2', 25), ('3 0 losers', 25)])
        subject = fics.play_policy(seeks=['5 0 100%', '3 0 w2', '3 0 losers'], seeks_at_once=3)
        self.assertEqual(subject.seek_commands(), ['seek 5 0 u'])
        self.assertEqual(fics.seek_weights(['5 0 99.9%', '3 0 w3']), [('5 0', 99.9), ('3 0 w3', 0.1)])

    def test_weighted_draws_and_distinct_ads(self):
        import random
        from collections import Counter
        subject = fics.play_policy(seeks=['5 0 50%', '3 0 w2', '3 0 w3'], seeks_at_once=1)
        with patch.object(fics, 'random', random.Random(20261026)):
            counts = Counter(subject.seek_commands()[0] for _ in range(4000))
            self.assertTrue(1900 < counts['seek 5 0 u'] < 2100, counts)
            for kind in ('w2', 'w3'):
                self.assertTrue(900 < counts[f'seek 3 0 {kind} u'] < 1100, counts)
            subject.seeks_at_once = 3
            self.assertEqual(len(set(subject.seek_commands())), 3)

    def test_invalid_percentage_pools(self):
        for pool in (['3 0 101%'], ['3 0 -1%'], ['3 0 nan%'], ['3 0 60%', '5 0 50%'],
                     ['3 0 50%'], ['3 0 0%'], ['3 0 50%', '3 0 50%'], ['3 0 50% w3']):
            with self.subTest(pool=pool), self.assertRaises(ValueError):
                fics.seek_weights(pool)

    async def test_operator_weights_are_validated_before_replacement(self):
        subject = player(owner='hof', seeks=['3 0'], seeks_at_once=3)
        await subject.handle('hof tells you: seek-pool 5 0 100%, 3 0 w3')
        self.assertIn('seek 5 0 u', subject.connection.sent)
        self.assertFalse(any('w3' in line for line in subject.connection.sent if line.startswith('seek ')))
        original = subject.policy.seek_pool[:]
        await subject.handle('hof tells you: seek-pool 5 0 80%, 3 0 w3 30%')
        self.assertEqual(subject.policy.seek_pool, original)

    async def test_reseek_requires_operator_and_waits_during_game(self):
        subject = player(owner='hof', follow_key='secret', seeks=['5 0 100%', '3 0 w3'], seeks_at_once=3)
        await subject.handle('stranger tells you: reseek')
        self.assertEqual(subject.connection.sent, [])
        await subject.handle('hof tells you: reseek')
        self.assertEqual(subject.connection.sent[:2], ['unseek', 'seek 5 0 u'])
        subject.connection.sent.clear()
        await subject.handle('stranger tells you: secret reseek')
        self.assertEqual(subject.connection.sent[:2], ['unseek', 'seek 5 0 u'])
        subject.connection.sent.clear()
        subject.game = 29
        await subject.handle('hof tells you: reseek')
        self.assertEqual(len(subject.connection.sent), 1)
        self.assertIn('Game in progress', subject.connection.sent[0])
