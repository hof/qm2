"""Seek pool, challenge filter, finger titles, rematch limit, stopping, and JSON settings."""
import asyncio
import json
from pathlib import Path
import tempfile
import types
import unittest

import chess.engine

from maxima_runner import fics
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
        self.assertCountEqual(subject.connection.sent[1:], ["seek 3 0 u", "seek 5 2 r", "seek 1 0 unrated"])
        registered = player(seeks=["3 0"], guest=False)
        await registered.on_login("Maxima")
        self.assertEqual(registered.connection.sent[-2:], ["unseek", "seek 3 0"])

    async def test_registered_login_sets_finger_notes(self):
        subject = player(seeks=["3 0"], guest=False, owner=["hof", "hajewiet"], notes=["Maxima plays unrated tests"])
        protocol = type("protocol", (), {"id": {"name": "Maxima 2.1"}, "config": {"Hash": 256},
                                         "options": {"Threads": types.SimpleNamespace(default=1)}})
        subject.session = types.SimpleNamespace(protocol=protocol)
        await subject.on_login("Maxima")
        self.assertEqual(subject.connection.sent[:2], ["set 1 Maxima 2.1 on Maxima runner",
                                                       "set 2 Owner: hof and hajewiet"])
        self.assertRegex(subject.connection.sent[2], r"^set 3 Hardware: .+, 1 thread, Hash 256 MB, \w+$")
        self.assertEqual(subject.connection.sent[3:], ["set 4 Maxima plays unrated tests", "unseek", "seek 3 0"])
        guest = player(seeks=["3 0"])
        await guest.on_login("GuestTEST")
        self.assertEqual(guest.connection.sent, ["unseek", "seek 3 0 u"])

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
    def write(self, value):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "fics.json"
        path.write_text(json.dumps(value))
        return str(path)

    def test_merges_file_over_defaults(self):
        settings = fics.load_settings(self.write({"seeks": ["1 0"], "games": 5, "options": {"Hash": 256}}))
        self.assertEqual((settings["seeks"], settings["games"], settings["options"]), (["1 0"], 5, {"Hash": "256"}))
        self.assertEqual(settings["host"], "freechess.org")
        self.assertEqual(fics.load_settings(None), fics.default_settings)

    def test_rejects_unknown_keys_and_wrong_types(self):
        for value, message in (({"seek": ["3 0"]}, "unknown"), ({"games": "5"}, "games must be int"),
                               ({"rematches": True}, "rematches must be int"), ({"seeks": [3]}, "strings"), ({"notes": ["x"] * 8}, "at most 7"), ({"owner": 5}, "owner must be str or list"),
                               ({"owner": ["hof", 5]}, "list of handles"),
                               ([], "JSON object")):
            with self.assertRaisesRegex(ValueError, message):
                fics.load_settings(self.write(value))


if __name__ == "__main__":
    unittest.main()
