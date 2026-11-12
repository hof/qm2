"""Packed state preservation, variant boundaries and damaged-file rejection."""
from pathlib import Path
import struct
import tempfile
import unittest

import chess

from maxima_runner import nnue_binary, nnue_data


class nnue_binary_tests(unittest.TestCase):
    def row(self, board, **fields):
        return dict(board=board, score_cp=-123, result_stm=None, split="validation", depth=31,
                    nodes=2**40, source={"sha256": "a" * 64, "game": 1, "ply": 57}, **fields)

    def metadata(self, variant="standard", **fields):
        return dict(variant=variant, score_pov="side_to_move", **fields)

    def test_known_nibble_order_and_dense_board_without_piece_count_limit(self):
        start = nnue_binary.pack_board(chess.Board())
        self.assertEqual(start[:8], bytes.fromhex("2453364211111111"))
        self.assertEqual(start[8:24], bytes(16))
        self.assertEqual(start[24:], bytes.fromhex("777777778ab99ca8"))
        self.assertEqual(nnue_binary.record.size, 64)
        board = chess.Board(None)
        for square in range(64):
            board.set_piece_at(square, chess.Piece(chess.PAWN, square % 2 == 0))
        recovered = nnue_binary.unpack_board(nnue_binary.pack_board(board))
        self.assertEqual(recovered.piece_map(), board.piece_map())
        with self.assertRaises(ValueError):
            nnue_binary.unpack_board(bytes([0x0d]) + bytes(31))

    def test_full_record_preserves_castling_ep_clocks_labels_and_source(self):
        board = chess.Board("r3k2r/8/8/3pP3/8/8/8/R3K2R w KQkq d6 49 117")
        board.chess960 = True
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.mxdata"
            nnue_binary.write_dataset(path, self.metadata("chess960"),
                                      [dict(self.row(board), result_stm=value) for value in (None, 0, 0.5, 1)])
            with nnue_binary.binary_dataset(path) as data:
                self.assertEqual(data.header["record_size"], 64)
                self.assertEqual(len(data.header["sources"]), 1)
                self.assertEqual([item["result_stm"] for item in data], [None, 0, 0.5, 1])
                row = data[0]
                self.assertEqual(row["board"].fen(en_passant="fen"), board.fen(en_passant="fen"))
                self.assertEqual(row["board"].castling_rights, board.castling_rights)
                self.assertTrue(row["board"].chess960)
                for key in ("score_cp", "result_stm", "split", "depth", "nodes", "source"):
                    self.assertEqual(row[key], self.row(board)[key])
            with self.assertRaises(ValueError):
                nnue_data.read_dataset(path)

    def test_promoted_and_variant_state_extensions_roundtrip_and_reject_silent_loss(self):
        board = chess.Board(None)
        for square in range(52):
            board.set_piece_at(square, chess.Piece(chess.PAWN, square % 2 == 0))
        board.promoted = chess.BB_A1
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "variant.mxdata"
            row = self.row(board, variant_state=b"\x02\x01")
            with self.assertRaises(ValueError):
                nnue_binary.write_dataset(path, self.metadata("future_variant"), [row])
            self.assertFalse(path.exists())
            nnue_binary.write_dataset(path, self.metadata("future_variant", variant_state_schema="counters_v1"),
                                      [row], store_promoted=True, variant_state_size=2)
            with nnue_binary.binary_dataset(path) as data:
                restored = data[0]
                self.assertEqual(len(restored["board"].piece_map()), 52)
                self.assertEqual(restored["board"].promoted, chess.BB_A1)
                self.assertEqual(restored["variant_state"], b"\x02\x01")
                self.assertEqual(data.header["record_size"], 74)
            with self.assertRaises(ValueError):
                nnue_data.read_dataset(path)

    def test_truncation_reserved_flags_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "data.mxdata"
            nnue_binary.write_dataset(path, self.metadata(), [self.row(chess.Board())])
            raw = path.read_bytes()
            with self.assertRaises(FileExistsError):
                nnue_binary.write_dataset(path, self.metadata(), [])
            for damaged in (raw[:12], raw[:-1], raw + b"\0", raw[:8] + struct.pack("<I", 99) + raw[12:]):
                path.write_bytes(damaged)
                with self.assertRaises(ValueError):
                    nnue_binary.binary_dataset(path)
            corrupted = bytearray(raw)
            corrupted[-64 + 47] |= 0x80
            path.write_bytes(corrupted)
            with nnue_binary.binary_dataset(path) as data:
                with self.assertRaises(ValueError):
                    data[0]

    def test_standard_data_interchange_preserves_every_training_field(self):
        board = chess.Board()
        original = dict(self.row(board), fen=board.fen(), split=nnue_data.position_split(board))
        original.pop("board")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "standard.mxdata"
            header = self.metadata(positions=1)
            nnue_data.save_dataset(path, header, [original])
            _, rows = nnue_data.read_dataset(path)
            self.assertEqual(rows, [original])
            # Generic storage may hold52 pieces; the current standard trainer must reject it.
            dense = chess.Board(None)
            for square in range(52):
                dense.set_piece_at(square, chess.Piece(chess.PAWN, True))
            other = Path(directory) / "dense.mxdata"
            nnue_binary.write_dataset(other, header, [self.row(dense)])
            with self.assertRaises(ValueError):
                nnue_data.read_dataset(other)
