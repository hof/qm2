"""Position text: FEN square order, cross-rank empty runs, implicit empty tail."""
import chess


def encode(board):
    if board.chess960 or not board.is_valid():
        raise ValueError("shortfen requires a valid standard-chess position")
    fields = board.fen(en_passant="fen").split()
    pieces, empty = [], 0
    for rank in range(7, -1, -1):
        for file in range(8):
            piece = board.piece_at(chess.square(file, rank))
            if piece is None:
                empty += 1
            else:
                if empty:
                    pieces.append(str(empty))
                    empty = 0
                pieces.append(piece.symbol())
    # No final run: the rest of the 64 squares are empty by definition.
    return " ".join(["".join(pieces), *fields[1:]])


def decode(text):
    if not isinstance(text, str):
        raise ValueError("shortfen must be text")
    fields = text.split()
    if len(fields) != 6:
        raise ValueError("shortfen requires six fields")
    squares, index = [], 0
    placement = fields[0]
    while index < len(placement):
        char = placement[index]
        if char in "pnbrqkPNBRQK":
            squares.append(char)
            index += 1
        elif "1" <= char <= "9":
            end = index + 1
            while end < len(placement) and "0" <= placement[end] <= "9":
                end += 1
            if end - index > 2:
                raise ValueError("shortfen empty run exceeds the board")
            count = int(placement[index:end])
            if count > 64 - len(squares):
                raise ValueError("shortfen empty run exceeds the board")
            squares.extend([None] * count)
            index = end
        else:
            raise ValueError(f"invalid shortfen character: {char!r}")
        if len(squares) > 64:
            raise ValueError("shortfen exceeds 64 squares")
    squares.extend([None] * (64 - len(squares)))
    ranks = []
    for start in range(0, 64, 8):
        rank, empty = [], 0
        for piece in squares[start:start + 8]:
            if piece is None:
                empty += 1
            else:
                if empty:
                    rank.append(str(empty))
                    empty = 0
                rank.append(piece)
        if empty:
            rank.append(str(empty))
        ranks.append("".join(rank))
    board = chess.Board(" ".join(["/".join(ranks), *fields[1:]]))
    if not board.is_valid():
        raise ValueError("invalid standard-chess position in shortfen")
    return board
