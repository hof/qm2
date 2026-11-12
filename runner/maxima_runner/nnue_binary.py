"""Versioned, fixed-size training records with generic 64-square board storage."""
import json
import mmap
import os
from pathlib import Path
import struct
import tempfile

import chess


magic = b"MXNNUEB\0"
prefix = struct.Struct("<8sIIQ")
record = struct.Struct("<32sQHHhBBBBQIH")
max_metadata_size = 64 * 1024 * 1024


def integer(value, minimum, maximum, name):
    if isinstance(value, bool) or not isinstance(value, int) or not minimum <= value <= maximum:
        raise ValueError(f"invalid binary dataset {name}")
    return value


def pack_board(board):
    """Low nibble first, a1..h8; codes0empty,1..6white,7..12black."""
    data = bytearray(32)
    for square, piece in board.piece_map().items():
        code = piece.piece_type + (0 if piece.color else 6)
        if not 1 <= code <= 12:
            raise ValueError("unsupported binary board piece type")
        data[square // 2] |= code << (4 * (square % 2))
    return bytes(data)


def unpack_board(data):
    if len(data) != 32:
        raise ValueError("binary board requires 32 bytes")
    board = chess.Board(None)
    for square in range(64):
        code = (data[square // 2] >> (4 * (square % 2))) & 15
        if code > 12:
            raise ValueError("reserved binary board piece code")
        if code:
            board.set_piece_at(square, chess.Piece((code - 1) % 6 + 1, code <= 6))
    return board


def write_dataset(path, metadata, rows, *, store_promoted=False, variant_state_size=0):
    """Write bounded-memory records; source/game metadata is shared in the header.

    Generic storage validates representation, not a variant's chess rules. Extra
    variant state is opaque, fixed-width and requires an explicit schema name.
    """
    integer(variant_state_size, 0, 65536, "variant state size")
    if not isinstance(store_promoted, bool):
        raise ValueError("store_promoted must be boolean")
    if not isinstance(metadata.get("variant"), str) or not metadata["variant"]:
        raise ValueError("binary dataset requires a variant identity")
    if metadata.get("score_pov") != "side_to_move":
        raise ValueError("binary dataset requires side-to-move scores")
    if variant_state_size and not metadata.get("variant_state_schema"):
        raise ValueError("extra variant state requires a schema name")
    path = Path(path)
    if path.exists():
        raise FileExistsError(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    sources, source_ids, count = [], {}, 0
    temporary = None
    try:
        with tempfile.TemporaryFile(dir=path.parent) as records:
            for row in rows:
                board = row.get("board")
                if board is None:
                    board = chess.Board(row["fen"])
                if type(board) is not chess.Board:
                    raise ValueError("use a generic board plus explicit variant state for storage")
                if board.promoted & ~board.occupied:
                    raise ValueError("promoted mask references an empty square")
                if board.promoted and not store_promoted:
                    raise ValueError("promoted markers require store_promoted")
                state = row.get("variant_state", b"")
                if not isinstance(state, bytes) or len(state) != variant_state_size:
                    raise ValueError("binary variant state has the wrong size")
                source = dict(row["source"])
                ply = integer(source.pop("ply"), 0, 65535, "source ply")
                key = json.dumps(source, sort_keys=True, allow_nan=False)
                if key not in source_ids:
                    source_ids[key] = len(sources)
                    sources.append(source)
                split = row["split"]
                if split not in ("train", "validation"):
                    raise ValueError("invalid binary dataset split")
                result = row.get("result_stm")
                if result is not None and (isinstance(result, bool) or result not in (0, 0.5, 1)):
                    raise ValueError("invalid binary dataset result")
                result_code = 3 if result is None else int(result * 2)
                flags = int(board.turn) | ((split == "validation") << 1) | (int(board.chess960) << 2)
                ep = 64 if board.ep_square is None else board.ep_square
                records.write(record.pack(pack_board(board),
                    integer(board.castling_rights, 0, 2**64 - 1, "castling rights"),
                    integer(board.halfmove_clock, 0, 65535, "halfmove clock"),
                    integer(board.fullmove_number, 1, 65535, "fullmove number"),
                    integer(row["score_cp"], -32768, 32767, "score"), result_code, flags,
                    integer(ep, 0, 64, "en passant square"),
                    integer(row.get("depth", 0), 0, 255, "depth"),
                    integer(row.get("nodes", 0), 0, 2**64 - 1, "nodes"), source_ids[key], ply))
                if store_promoted:
                    records.write(struct.pack("<Q", board.promoted))
                records.write(state)
                count += 1
            header = dict(metadata, format="maxima.nnue.binary", version=1, positions=count,
                          board_encoding="nibbles_a1", record_size=record.size + 8 * store_promoted + variant_state_size,
                          store_promoted=store_promoted, variant_state_size=variant_state_size, sources=sources)
            encoded = json.dumps(header, sort_keys=True, allow_nan=False).encode("utf-8")
            if len(encoded) > max_metadata_size:
                raise ValueError("binary metadata exceeds 64 MiB; use smaller shards")
            with tempfile.NamedTemporaryFile(mode="wb", dir=path.parent, prefix=f".{path.name}.",
                                             delete=False) as output:
                temporary = Path(output.name)
                output.write(prefix.pack(magic, 1, len(encoded), count))
                output.write(encoded)
                records.seek(0)
                for block in iter(lambda: records.read(1024 * 1024), b""):
                    output.write(block)
            os.link(temporary, path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


class binary_dataset:
    """Memory-mapped, indexed record access; callers control batch size."""

    def __init__(self, path):
        self.stream = Path(path).open("rb")
        self.mapping = None
        try:
            raw = self.stream.read(prefix.size)
            if len(raw) != prefix.size:
                raise ValueError("truncated binary dataset header")
            signature, version, metadata_size, count = prefix.unpack(raw)
            if signature != magic or version != 1 or metadata_size > max_metadata_size:
                raise ValueError("unsupported binary dataset header")
            metadata = self.stream.read(metadata_size)
            if len(metadata) != metadata_size:
                raise ValueError("truncated binary dataset metadata")
            self.header = json.loads(metadata)
            h = self.header
            if not isinstance(h, dict):
                raise ValueError("binary dataset metadata must be an object")
            # A variant's legality is deliberately outside this generic container.
            if (h.get("format") != "maxima.nnue.binary" or h.get("version") != 1
                    or h.get("positions") != count or h.get("board_encoding") != "nibbles_a1"
                    or not isinstance(h.get("variant"), str) or not h["variant"]
                    or h.get("score_pov") != "side_to_move" or not isinstance(h.get("sources"), list)
                    or not all(isinstance(source, dict) and "ply" not in source for source in h["sources"])
                    or not isinstance(h.get("store_promoted"), bool)):
                raise ValueError("invalid binary dataset metadata")
            state_size = integer(h.get("variant_state_size"), 0, 65536, "variant state size")
            if state_size and not h.get("variant_state_schema"):
                raise ValueError("missing binary variant state schema")
            self.record_size = record.size + 8 * h["store_promoted"] + state_size
            self.offset = prefix.size + metadata_size
            if (h.get("record_size") != self.record_size
                    or os.fstat(self.stream.fileno()).st_size != self.offset + count * self.record_size):
                raise ValueError("binary dataset size/count mismatch")
            self.count = count
            self.mapping = mmap.mmap(self.stream.fileno(), 0, access=mmap.ACCESS_READ)
        except BaseException:
            self.close()
            raise

    def __len__(self):
        return self.count

    def __getitem__(self, index):
        if index < 0:
            index += self.count
        if not 0 <= index < self.count:
            raise IndexError(index)
        offset = self.offset + index * self.record_size
        squares, castling, halfmove, fullmove, score, result, flags, ep, depth, nodes, source, ply = record.unpack_from(self.mapping, offset)
        if flags & ~7 or ep > 64 or result > 3 or fullmove == 0 or source >= len(self.header["sources"]):
            raise ValueError("invalid binary dataset record")
        board = unpack_board(squares)
        board.castling_rights = castling
        board.halfmove_clock, board.fullmove_number = halfmove, fullmove
        board.turn, board.chess960 = bool(flags & 1), bool(flags & 4)
        board.ep_square = None if ep == 64 else ep
        start = offset + record.size
        if self.header["store_promoted"]:
            board.promoted = struct.unpack_from("<Q", self.mapping, start)[0]
            if board.promoted & ~board.occupied:
                raise ValueError("binary promoted mask references an empty square")
            start += 8
        return {"board": board, "score_cp": score, "result_stm": None if result == 3 else result / 2,
                "split": "validation" if flags & 2 else "train", "depth": depth, "nodes": nodes,
                "source": dict(self.header["sources"][source], ply=ply),
                "variant_state": self.mapping[start:offset + self.record_size]}

    def close(self):
        if self.mapping is not None:
            self.mapping.close()
            self.mapping = None
        self.stream.close()

    def __enter__(self):
        return self

    def __exit__(self, *error):
        self.close()
