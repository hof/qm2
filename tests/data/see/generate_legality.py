"""Independent legal-move certificates for the least-valuable exchange model.

Requires python-chess only when regenerating. Declining an exchange is allowed,
even in check: this is a material model, not a complete game-tree search.
A full same-square minimax supplies a separate check of the targeted fixtures.
"""
import argparse
import json
import random
from pathlib import Path
import chess

values = {chess.PAWN: 100, chess.KNIGHT: 325, chess.BISHOP: 325,
          chess.ROOK: 500, chess.QUEEN: 925, chess.KING: 10000}

def material(board, side):
    return sum(values[p.piece_type] * (1 if p.color == side else -1)
               for p in board.piece_map().values())

def exchanges(board, target, exhaustive=False):
    moves = [m for m in board.legal_moves if m.to_square == target and board.is_capture(m)]
    if not moves:
        return 0
    if not exhaustive:
        # Piece type resolves the equal bishop/knight value; square resolves ties.
        moves = [min(moves, key=lambda m: (values[board.piece_type_at(m.from_square)],
                     board.piece_type_at(m.from_square), m.from_square,
                     -values.get(m.promotion, 0)))]
    side = board.turn
    before = material(board, side)
    result = 0
    for move in moves:
        board.push(move)
        result = max(result, material(board, side) - before - exchanges(board, target, exhaustive))
        board.pop()
    return result

def evaluate(board, move, exhaustive=False):
    if board.is_castling(move):
        return 0
    side = board.turn
    before = material(board, side)
    board.push(move)
    result = material(board, side) - before - exchanges(board, move.to_square, exhaustive)
    board.pop()
    return result

seeds = [
    ('pinned_pawn', 'rnb1kbnr/ppp3pp/4p3/3p4/8/2N5/PPP2PPP/R3R1K1 w - - 0 1', 'c3d5'),
    ('unpinned_pawn', 'rnb1kbnr/ppp3pp/4p3/3p4/8/2N5/PPP2PPP/R4RK1 w - - 0 1', 'c3d5'),
    ('pinned_knight', 'rnb1kb1r/ppp1n1pp/8/3p4/8/2N5/PPP2PPP/R3R1K1 w - - 0 1', 'c3d5'),
    ('pinned_bishop', 'rnb1k1nr/ppp3pp/4b3/3p4/8/2N5/PPP2PPP/R3R1K1 w - - 0 1', 'c3d5'),
    ('pinned_rook', 'rnb1kbn1/ppp3pp/8/3pr3/8/2N5/PPP2PPP/R3R1K1 w - - 0 1', 'c3d5'),
    ('pin_line_capture', '6k1/8/4b3/3p4/2B5/8/8/K7 w - - 0 1', 'c4d5'),
    ('released_pin', '3qr1k1/2B5/4N3/8/8/8/8/4K3 w - - 0 1', 'c7d8'),
    ('new_pin', '2r3k1/8/4b3/2Rp4/8/2N5/8/2K5 w - - 0 1', 'c5d5'),
    ('king_defended_target', '6k1/5p2/8/8/2B5/8/8/4KR2 w - - 0 1', 'c4f7'),
    ('king_pinned_defender', '6k1/5p2/8/8/2B5/8/8/4KR1r w - - 0 1', 'c4f7'),
    ('king_legal_capture', '6k1/5p2/8/8/2B5/8/8/4K3 w - - 0 1', 'c4f7'),
    ('en_passant_discovered_check', '4k3/2p5/8/3pP3/8/8/8/4R1K1 w - d6 0 1', 'e5d6'),
    ('recapture_promotes', '1R4k1/P7/8/1r6/8/8/8/K7 b - - 0 1', 'b5b8'),
    ('historical_queen', 'r4rk1/p5p1/1p2p2p/3pPn1P/b1p3Bq/P1PQN1P1/2P2P2/R4RK1 w - - 0 2', 'd3f5'),
]
for promotion in 'qrbn':
    seeds.append(('promotion_'+promotion, '1r4r1/P6k/8/8/8/8/8/K7 w - - 0 1', 'a7b8'+promotion))

cases = []
full_differences = []
def append_case(name, board, move, targeted):
    assert board.is_valid(), (name, board.status(), board.fen())
    assert move in board.legal_moves, (name, board.fen(), move)
    expected = evaluate(board, move)
    full = evaluate(board, move, True)
    if targeted:
        assert expected == full, (name, expected, full)
    if expected != full:
        full_differences.append((board.fen(), move.uci(), expected, full))
    cases.append((name, board.fen(en_passant='fen'), move.uci(), expected))

for name, fen, text in seeds:
    board = chess.Board(fen)
    move = chess.Move.from_uci(text)
    append_case(name, board, move, True)
    mirrored = board.mirror()
    mirror_move = chess.Move(move.from_square ^ 56, move.to_square ^ 56, promotion=move.promotion)
    append_case(name+'_black', mirrored, mirror_move, True)

randomizer = random.Random(20261015)
for game in range(24):
    board = chess.Board()
    for ply in range(100):
        moves = sorted(board.legal_moves, key=lambda m: m.uci())
        if not moves or board.is_game_over():
            break
        if ply % 5 == 0:
            captures = [m for m in moves if board.is_capture(m) or m.promotion]
            for move in captures[:4]:
                append_case('sample_%d_%d_%s' % (game, ply, move.uci()), board, move, False)
            append_case('quiet_%d_%d' % (game, ply), board, randomizer.choice(moves), False)
        board.push(randomizer.choice(moves))

root = Path(__file__).resolve().parent
rows = ['// Generated independently with python-chess; regenerate with generate_legality.py.']
rows += ['{%s, %s, %s, %d},' % (json.dumps(n), json.dumps(f), json.dumps(m), v) for n,f,m,v in cases]
output = '\n'.join(rows)+'\n'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--check', action='store_true')
args = parser.parse_args()
if args.check:
    if (root/'legality.inc').read_text() != output:
        raise SystemExit('legal exchange certificates differ')
else:
    (root/'legality.inc').write_text(output)
print(json.dumps({'cases':len(cases), 'targeted_cases':2*len(seeds),
                 'least_valuable_vs_full_minimax_differences':len(full_differences),
                 'example':full_differences[:3]}, indent=2))
