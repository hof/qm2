"""Play on FICS as MaxDrone: seek, play, save PGN, and learn from every game."""
import argparse


def parser():
    """maxima fics arguments; built before python-chess loads, so --help works without it."""
    parser = argparse.ArgumentParser(prog="maxima fics", description=__doc__)
    parser.add_argument("--config", metavar="FILE",
                        help="JSON settings (keys: host, port, handle, engine, options, seeks, seeks_at_once, "
                             "accept, rematches, games, owner, notes, pgn_dir, learning, variety, contempt_window, "
                             "layer_profiles, style_books); "
                             "flags override it")
    parser.add_argument("--host")
    parser.add_argument("--port", type=int)
    parser.add_argument("--handle", help="registered handle (password from FICS_PASSWORD), default guest")
    parser.add_argument("--no-timeseal", dest="timeseal", action="store_false",
                        help="send plain lines; the server then charges network lag to our clock")
    parser.add_argument("--probe-interval", type=float, default=300,
                        help="seconds of server silence before a liveness probe (default: 300)")
    parser.add_argument("--debug", metavar="FILE", help="append a wire transcript (password masked)")
    parser.add_argument("--engine", help="UCI engine to play with (default: the Release Maxima build)")
    parser.add_argument("--option", action="append", default=[], metavar="NAME=VALUE",
                        help="UCI option for the engine, repeatable (e.g. Hash=256)")
    parser.add_argument("--no-engine", action="store_true", help="connect without playing games")
    parser.add_argument("--seek", action="append", metavar="SPEC",
                        help='seek pool entry, repeatable (e.g. "3 0"; default pool 3 0, 3 1, 5 0)')
    parser.add_argument("--seeks-at-once", type=int, help="seeks posted at a time, drawn from the pool (default: 2)")
    parser.add_argument("--no-seek", action="store_true", help="do not seek; only accept challenges")
    parser.add_argument("--no-accept", action="store_true", help="decline all challenges")
    parser.add_argument("--rematches", type=int, help="rematches in a row per opponent (default: 0)")
    parser.add_argument("--games", type=int, help="quit after this many games")
    parser.add_argument("--pgn-dir", help="directory for monthly PGN files (default: experience/games/fics)")
    parser.add_argument("--no-pgn", action="store_true", help="do not save games")
    parser.add_argument("--no-learning", action="store_true",
                        help="let the engine use its own book instead of MaxDrone's experience")
    parser.add_argument("--variety", type=float,
                        help="opening variety: above 1 explores more, below 1 repeats winners (default: 1)")
    return parser


def main():
    arguments = parser()
    options = arguments.parse_args()
    from . import fics
    return fics.run(options, arguments)
