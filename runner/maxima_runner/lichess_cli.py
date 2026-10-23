"""Play on Lichess as a bot: accept challenges, play them with Maxima, save PGN, and learn from every game."""
import argparse


def parser():
    """maxima lichess arguments; built before python-chess loads, so --help works without it."""
    parser = argparse.ArgumentParser(
        prog="maxima lichess", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="environment (or .env in the checkout):\n"
               "  LICHESS_BOT_TOKEN    token of the bot account, scope bot:play (never logged)\n"
               "  LICHESS_OWNERS       Lichess users who may send commands, comma-separated\n"
               "  MAXDRONE_POST_TOKEN  bearer token for --pgn-post (never logged)\n\n"
               "commands for anyone, in the game's chat: !help, !name, !eval (spectator room only)\n"
               "commands from an owner, typed in the game's chat (spectator room is best):\n"
               "  !softquit      stop after this game\n"
               "  !softrestart   restart after this game (exit 75, for a supervisor loop)\n"
               "  !resign, !abort, !draw   act on the game now\n"
               "between games: type quit here, or kill -USR1 <pid>, to stop\n\n"
               "examples:\n"
               "  maxima lichess\n"
               "  maxima lichess --speed blitz --speed rapid --games 20 --live-feed 8765")
    parser.add_argument("--config", metavar="FILE",
                        help="JSON settings (keys: engine, options, speeds, accept, games, pgn_dir, learning, variety, "
                             "contempt_window, layer_profiles, style_books, mood, live_feed, pgn_post, "
                             "online_tablebase); flags override it")
    parser.add_argument("--engine", help="UCI engine to play with (default: the Release Maxima build)")
    parser.add_argument("--option", action="append", default=[], metavar="NAME=VALUE",
                        help="UCI option for the engine, repeatable (e.g. Hash=256)")
    parser.add_argument("--speed", action="append", choices=("bullet", "blitz", "rapid", "classical"),
                        help="accepted speed, repeatable (default: bullet, blitz, rapid)")
    parser.add_argument("--no-accept", action="store_true", help="decline all challenges")
    parser.add_argument("--games", type=int, help="stop after this many games")
    parser.add_argument("--pgn-dir", help="directory for monthly PGN files (default: experience/lichess/games)")
    parser.add_argument("--no-pgn", action="store_true", help="do not save games")
    parser.add_argument("--no-learning", action="store_true",
                        help="let the engine use its own book instead of MaxDrone's experience")
    parser.add_argument("--no-online-tablebase", action="store_true",
                        help="search endgames with up to seven pieces instead of asking tablebase.lichess.ovh")
    parser.add_argument("--live-feed", metavar="[HOST:]PORT",
                        help="serve game events as JSON over WebSocket (default host 127.0.0.1; MAXIMA.md 11)")
    parser.add_argument("--pgn-post", metavar="URL", help="POST each finished game as JSON to this URL (MAXIMA.md 11)")
    parser.add_argument("--variety", type=float,
                        help="opening variety: above 1 explores more, below 1 repeats winners (default: 1)")
    parser.add_argument("--server", default="https://lichess.org", help=argparse.SUPPRESS)
    return parser


def main():
    arguments = parser()
    options = arguments.parse_args()
    from . import lichess
    return lichess.run(options, arguments)
