"""Play on FICS as MaxDrone: seek, play, save PGN, and learn from every game."""
import argparse


def parser():
    """maxima fics arguments; built before python-chess loads, so --help works without it."""
    parser = argparse.ArgumentParser(
        prog="maxima fics", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="environment:\n"
               "  FICS_PASSWORD   password for --handle (never logged)\n"
               "  MAXDRONE_KEY    remote-control key (masked in logs)\n"
               "  MAXDRONE_POST_TOKEN  bearer token for --pgn-post (never logged)\n\n"
               "remote control, from any FICS account:\n"
               "  tell <name> <key> follow       explain each move to you by tell\n"
               "  tell <name> unfollow           stop those tells\n"
               "  tell <name> <key> softquit     stop after this game\n"
               "  tell <name> <key> softrestart  restart after this game (exit 75, for a supervisor loop)\n"
               "  tell <name> <key> !abort|!resign|!draw   act on the current game now\n"
               "  tell <name> <key> reseek       withdraw ads and draw the pool again\n"
               "  tell <name> <key> seek-pool 5 0 50%, 3 0 w3  replace the mixed weighted pool\n"
               "  tell <name> <key> help         list the commands (configured owners may omit the key)\n"
               "typed here: lines go to FICS, except help, quit, softquit, softrestart and reseek\n\n"
               "examples:\n"
               "  maxima fics --whisper\n"
               "  maxima fics --handle MaxDrone --seek \"3 0\" --seek \"5 0\" --games 20")
    parser.add_argument("--pv-plies", type=int, metavar="N",
                        help="console PV replies after our move (default: 2; 0 hides PV); config: pv_plies")
    parser.add_argument("--shutdown-grace", type=float, metavar="SECONDS",
                        help="wait for the current game after SIGTERM/SIGINT, then save and close (default: 15)")
    parser.add_argument("--config", metavar="FILE",
                        help="JSON settings (default: first .fics.json in current, data, or project directory; "
                             "keys: host, port, handle, guest_name, engine, options, seeks, seeks_at_once, "
                             "accept, rematches, games, owner, notes, pgn_dir, learning, variety, contempt_window, "
                             "layer_profiles, style_books, first_move_wait, whisper, mood, live_feed, pgn_post, online_tablebase, "
                             "reseek_minutes, resignation, shutdown_grace, losers, losers_seeks, losers_seeks_at_once); "
                             "flags override it")
    parser.add_argument("--host")
    parser.add_argument("--port", type=int)
    parser.add_argument("--handle", help="registered handle (password from FICS_PASSWORD), default guest")
    parser.add_argument("--guest-name", help="guest login name (default: server-assigned random name)")
    parser.add_argument("--no-timeseal", dest="timeseal", action="store_false",
                        help="send plain lines; the server then charges network lag to our clock")
    parser.add_argument("--probe-interval", type=float, default=300,
                        help="seconds without traffic either way before a probe (default: 300)")
    parser.add_argument("--debug", metavar="FILE", help="append a wire transcript (password masked)")
    parser.add_argument("--engine", help="UCI engine to play with (default: the Release Maxima build)")
    parser.add_argument("--option", action="append", default=[], metavar="NAME=VALUE",
                        help="UCI option for the engine, repeatable (e.g. Hash=256)")
    parser.add_argument("--no-engine", action="store_true", help="connect without playing games")
    parser.add_argument("--seek", action="append", metavar="SPEC",
                        help='seek pool entry, repeatable (e.g. "3 0", "3 0 losers", "3 0 w3"; default pool 3 0, 3 1, 5 0)')
    parser.add_argument("--seeks-at-once", type=int, help="seeks posted at a time, drawn from the pool (default: 2)")
    parser.add_argument("--losers", action="store_true", default=None, help="also accept FICS royal-losers challenges")
    parser.add_argument("--losers-seek", action="append", metavar="SPEC",
                        help='enable losers and add a time control to its separate pool, e.g. "3 0 u"')
    parser.add_argument("--losers-seeks-at-once", type=int, help="losers seeks posted alongside standard seeks (default: 1)")
    parser.add_argument("--no-seek", action="store_true", help="do not seek; only accept challenges")
    parser.add_argument("--whisper", action="store_true",
                        help="whisper each move's reason to observers: book layer and mix, or depth, score, "
                             "time, speed and line (guests are muted; MaxDrone then stops whispering)")
    parser.add_argument("--no-accept", action="store_true", help="decline all challenges")
    parser.add_argument("--rematches", type=int, help="rematches in a row per opponent (default: 0)")
    parser.add_argument("--games", type=int, help="quit after this many games")
    parser.add_argument("--pgn-dir", help="directory for atomic per-game PGNs (default: experience/games)")
    parser.add_argument("--no-pgn", action="store_true", help="do not save games")
    parser.add_argument("--no-learning", action="store_true",
                        help="let the engine use its own book instead of MaxDrone's experience")
    parser.add_argument("--no-online-tablebase", action="store_true",
                        help="search endgames with up to seven pieces instead of asking tablebase.lichess.ovh")
    parser.add_argument("--live-feed", metavar="[HOST:]PORT",
                        help="serve game events as JSON over WebSocket (default host 127.0.0.1; MAXIMA.md 11)")
    parser.add_argument("--pgn-post", metavar="URL",
                        help="POST each finished game as JSON to this URL (MAXIMA.md 11)")
    parser.add_argument("--variety", type=float,
                        help="opening variety: above 1 explores more, below 1 repeats winners (default: 1)")
    return parser


def main():
    arguments = parser()
    options = arguments.parse_args()
    from . import fics
    return fics.run(options, arguments)
