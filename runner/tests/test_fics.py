"""FICS transport, timeseal, login, keepalive, and reconnect checks against a local fake server."""
import asyncio
import types
import unittest

from maxima_runner import fics


def timeseal_decode(data):
    """Independent inverse of the encoding, as the server applies it."""
    offset = data[-1] & 0x7f
    buffer = bytearray((((byte + 32) & 0xff) ^ fics.timeseal_key[(index + offset) % len(fics.timeseal_key)]) & 0x7f
                       for index, byte in enumerate(data[:-1]))
    for block in range(0, len(buffer), 12):
        for first, second in ((0, 11), (2, 9), (4, 7)):
            buffer[block + first], buffer[block + second] = buffer[block + second], buffer[block + first]
    line, rest = bytes(buffer).split(b"\x18", 1)
    return line, int(rest.split(b"\x19", 1)[0]), offset


class timeseal_tests(unittest.TestCase):
    def test_matches_vectors_accepted_by_a_live_server(self):
        self.assertEqual(fics.timeseal_encode(b"guest", 12345, 7).hex(),
                         "d46574bbd4b1d2877aa5a3b67f61717c71a1a3beb6a3b0bc87")
        self.assertEqual(fics.timeseal_encode(b"e2e4", 987654, 49).hex(),
                         "97c6bcb9b2aaacd9bb7574add7d8d2c27871a7607f61717cb1")

    def test_pads_aligned_input_with_a_full_block(self):
        self.assertEqual(len(fics.timeseal_encode(b"abcd", 123456, 0)), 25)

    def test_round_trips_through_server_decoding(self):
        for line in (b"", b"\x029", b"set style 12", b"x" * 40):
            for offset in (0, 13, 49):
                self.assertEqual(timeseal_decode(fics.timeseal_encode(line, 4242, offset)), (line, 4242, offset))


class line_buffer_tests(unittest.TestCase):
    def test_splits_server_lines_and_strips_prompts(self):
        buffer = fics.line_buffer()
        self.assertEqual(buffer.feed(b"\n\rfics% fics% Style 12 set.\n\rfics% You will"), (["Style 12 set."], 0))
        self.assertEqual(buffer.partial, "You will")
        self.assertEqual(buffer.feed(b" not hear shouts.\n\r"), (["You will not hear shouts."], 0))

    def test_removes_telnet_negotiation_split_across_reads(self):
        buffer = fics.line_buffer()
        self.assertEqual(buffer.feed(b"\rPress return\n\r\xff"), (["Press return"], 0))
        self.assertEqual(buffer.feed(b"\xfc"), ([], 0))
        self.assertEqual(buffer.feed(b"\x01\n\rok \xff\xff\n"), (["ok \xff"], 0))

    def test_counts_timeseal_pings_split_across_reads(self):
        buffer = fics.line_buffer()
        self.assertEqual(buffer.feed(b"\n\r[G"), ([], 0))
        self.assertEqual(buffer.feed(b"]\x00\n\rfics% "), ([], 1))
        self.assertEqual(buffer.feed(b"\n\r[G]\n\r<12> board\n"), (["<12> board"], 1))

    def test_exposes_unterminated_login_prompt(self):
        buffer = fics.line_buffer()
        buffer.feed(b"\n\rlogin: ")
        self.assertEqual(buffer.partial, "login: ")
        buffer.clear_partial()
        self.assertEqual(buffer.partial, "")


class fake_server:
    """Scripted FICS stand-in; records decoded client lines per connection."""

    def __init__(self, script, timeseal=True):
        self.script = script
        self.timeseal = timeseal
        self.connections = []
        self.failures = []

    async def __aenter__(self):
        self.server = await asyncio.start_server(self.handle, "127.0.0.1", 0)
        self.port = self.server.sockets[0].getsockname()[1]
        return self

    async def __aexit__(self, *exc):
        self.server.close()
        await self.server.wait_closed()
        if self.failures and exc[0] is None:
            raise self.failures[0]

    async def handle(self, reader, writer):
        received = []
        self.connections.append(received)

        async def read_line():
            raw = await asyncio.wait_for(reader.readline(), 5)
            if not raw:
                raise ConnectionError("client closed")
            raw = raw.rstrip(b"\n")
            line = timeseal_decode(raw)[0] if self.timeseal else raw
            received.append(line.decode("latin-1"))
            return received[-1]

        try:
            if self.timeseal:
                assert (await read_line()).startswith("TIMESTAMP|MaxDrone|"), "missing timeseal hello"
                received.clear()
            await self.script(len(self.connections), writer, read_line)
        except (ConnectionError, asyncio.TimeoutError):
            pass
        except AssertionError as failure:
            self.failures.append(failure)
        finally:
            writer.close()


async def guest_login(writer, read_line, name="GuestTEST"):
    writer.write(b"\n\r   ****** Welcome ******\n\r\n\rlogin: ")
    assert await read_line() == "guest"
    writer.write(f'\n\rLogging you in as "{name}".\n\rPress return to enter the server as "{name}":\n'
                 .encode() + b"\r\xff\xfc\x01\n")
    assert await read_line() == ""
    writer.write(f"\r\n\r**** Starting FICS session as {name}(U) ****\n\rfics% ".encode())
    for _ in fics.login_settings:
        await read_line()


def options(port, **overrides):
    values = dict(host="127.0.0.1", port=port, handle="guest", password=None, timeseal=True, probe_interval=300)
    values.update(overrides)
    return types.SimpleNamespace(**values)


class login_tests(unittest.IsolatedAsyncioTestCase):
    async def test_guest_login_applies_settings_and_answers_pings(self):
        async def script(number, writer, read_line):
            await guest_login(writer, read_line)
            writer.write(b"\n\r[G]\x00\n\rfics% ")
            self.assertEqual(await read_line(), fics.timeseal_pong)
            writer.write(b"\n\rpong seen\n\rfics% ")
            self.assertEqual(await read_line(), "who")

        async with fake_server(script) as server:
            connection = await fics.ics_connection.open("127.0.0.1", server.port)
            try:
                self.assertEqual(await fics.login(connection, "guest", timeout=5), "GuestTEST")
                await connection.expect([fics.re.compile("pong seen")], 5)
                await connection.send("who")
                await asyncio.sleep(0.2)
            finally:
                await connection.close()
        self.assertEqual(server.connections[0][2:2 + len(fics.login_settings)], list(fics.login_settings))
        self.assertEqual(server.connections[0][-1], "who")

    async def test_registered_login_masks_password(self):
        async def script(number, writer, read_line):
            writer.write(b"\n\rlogin: ")
            self.assertEqual(await read_line(), "Maxima")
            writer.write(b'\n\r"Maxima" is a registered name.\n\rpassword: ')
            self.assertEqual(await read_line(), "secret")
            writer.write(b"\n\r**** Starting FICS session as Maxima(C) ****\n\r")
            for _ in fics.login_settings:
                await read_line()

        transcript = []
        async with fake_server(script, timeseal=False) as server:
            connection = await fics.ics_connection.open("127.0.0.1", server.port, False, transcript.append)
            try:
                self.assertEqual(await fics.login(connection, "Maxima", "secret", timeout=5), "Maxima")
            finally:
                await connection.close()
        self.assertNotIn("secret", "\n".join(transcript))
        self.assertIn("-> ********", transcript)

    async def test_rejected_logins_raise_clear_errors(self):
        async def wrong_password(number, writer, read_line):
            writer.write(b"\n\rlogin: ")
            await read_line()
            writer.write(b"\n\rpassword: ")
            await read_line()
            writer.write(b"\n\r**** Invalid password! ****\n\r")
            await asyncio.sleep(1)

        async def unregistered(number, writer, read_line):
            writer.write(b"\n\rlogin: ")
            await read_line()
            writer.write(b'\n\rPress return to enter the server as "Nobody":\n\r')
            await asyncio.sleep(1)

        for script, message in ((wrong_password, "Invalid password"), (unregistered, "not a registered")):
            async with fake_server(script, timeseal=False) as server:
                connection = await fics.ics_connection.open("127.0.0.1", server.port, False)
                try:
                    with self.assertRaisesRegex(fics.login_error, message):
                        await fics.login(connection, "Nobody", "secret", timeout=5)
                finally:
                    await connection.close()

    async def test_validates_handles_before_connecting(self):
        for handle in ("ab", "with space", "x" * 18, "Guest1"):
            with self.assertRaises(fics.login_error):
                await fics.login(None, handle, "secret")
        with self.assertRaisesRegex(fics.login_error, "needs a password"):
            await fics.login(None, "Maxima")

    async def test_rejects_multiline_commands(self):
        async def script(number, writer, read_line):
            await asyncio.sleep(1)

        async with fake_server(script) as server:
            connection = await fics.ics_connection.open("127.0.0.1", server.port)
            try:
                with self.assertRaises(ValueError):
                    await connection.send("tell x hi\nquit")
            finally:
                await connection.close()


class session_tests(unittest.IsolatedAsyncioTestCase):
    async def test_quiet_server_is_probed_then_dropped(self):
        async def script(number, writer, read_line):
            await guest_login(writer, read_line)
            self.assertEqual(await read_line(), "date")
            await asyncio.sleep(5)

        async with fake_server(script) as server:
            connection = await fics.ics_connection.open("127.0.0.1", server.port)
            try:
                await fics.login(connection, "guest", timeout=5)
                started = asyncio.get_event_loop().time()
                with self.assertRaisesRegex(ConnectionError, "stopped responding"):
                    await fics.serve(connection, asyncio.Queue(), lambda line: None,
                                     probe_interval=0.5, probe_timeout=1)
                self.assertLess(asyncio.get_event_loop().time() - started, 4)
            finally:
                await connection.close()

    async def test_reconnects_after_drop_and_quits_cleanly(self):
        async def script(number, writer, read_line):
            await guest_login(writer, read_line, f"GuestRUN{'AB'[number - 1]}")
            if number == 1:
                return
            self.assertEqual(await read_line(), "quit")

        lines = []
        commands = asyncio.Queue()

        def on_line(line):
            lines.append(line)
            if line == "-- logged in as GuestRUNB":
                commands.put_nowait("quit")

        async with fake_server(script) as server:
            result = await asyncio.wait_for(
                fics.run_client(options(server.port), commands, on_line, backoff=(0.1,)), 10)
        self.assertEqual(result, 0)
        self.assertEqual(len(server.connections), 2)
        self.assertTrue(any(line.startswith("-- connection lost") for line in lines))

    async def test_console_keeps_its_commands_and_hides_boards(self):
        async def script(number, writer, read_line):
            await guest_login(writer, read_line)
            writer.write(b"<12> board\n\rfics% ")
            self.assertEqual(await read_line(), "finger")
            self.assertEqual(await read_line(), "unseek")
            self.assertEqual(await read_line(), "quit")

        class idle_player:
            stopping = finished = restart = False
            game = None

            async def handle(self, line):
                pass

            async def stop(self):
                self.stopping = self.finished = True
                await self.connection.send("unseek")

        lines, commands, player = [], asyncio.Queue(), idle_player()
        for command in ("help", "finger", "softrestart"):
            commands.put_nowait(command)
        async with fake_server(script) as server:
            connection = await fics.ics_connection.open("127.0.0.1", server.port)
            try:
                await fics.login(connection, "guest", timeout=5)
                player.connection = connection
                self.assertEqual(await asyncio.wait_for(fics.serve(connection, commands, lines.append, player=player),
                                                        10), "quit")
            finally:
                await connection.close()
        self.assertTrue(player.restart)
        self.assertTrue(any(line.startswith("-- console:") for line in lines))
        self.assertFalse(any("<12>" in line for line in lines))


if __name__ == "__main__":
    unittest.main()
