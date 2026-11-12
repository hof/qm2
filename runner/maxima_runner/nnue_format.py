"""Legacy HalfKP feature mapping, export and scalar integer checks (no torch)."""
from array import array
import struct
import sys

import chess


feature_count = 41024
output_scale = 400 * 208 / 100 * 16 / 127
network_size = 21022697
transformer_start = 189
network_start = transformer_start + 4 + 512 + 2 * 256 * feature_count


def features(board, color):
    king = board.king(color)
    if king is None or not board.is_valid() or len(board.piece_map()) > 32:
        raise ValueError("HalfKP requires a valid standard board with at most 32 pieces")
    orientation = 0 if color else 63
    return [(square ^ orientation) + 1
            + 64 * (2 * (piece.piece_type - 1) + int(piece.color != color))
            + 641 * (king ^ orientation)
            for square, piece in sorted(board.piece_map().items()) if piece.piece_type != chess.KING]


def divide_toward_zero(value, divisor):
    return (1 if value >= 0 else -1) * (abs(value) // divisor)


def integer_array(kind, values):
    result = array(kind, values)
    if sys.byteorder != "little":
        result.byteswap()
    return result.tobytes()


def serialize(weights):
    """Pad the compact model into the existing 256x2 -> 32 -> 32 -> 1 loader."""
    width = len(weights["ft_bias"])
    hidden = len(weights["h1_bias"])
    if not 1 <= width <= 256 or not 1 <= hidden <= 32:
        raise ValueError("unsupported prototype dimensions")
    description = b"Maxima own CPU training prototype; compact HalfKP padded to legacy dimensions"
    data = bytearray(struct.pack("<III", 0x7af32f16, 0x3e5aa6ee, 177) + description.ljust(177, b" "))
    data.extend(struct.pack("<I", 0x5d69d7b8))
    data.extend(integer_array("h", weights["ft_bias"] + [0] * (256 - width)))
    if len(weights["ft_weight"]) != feature_count:
        raise ValueError("invalid feature count")
    for column in weights["ft_weight"]:
        if len(column) != width:
            raise ValueError("invalid feature column")
        data.extend(integer_array("h", column + [0] * (256 - width)))
    data.extend(struct.pack("<I", 0x63337156))
    for name, input_size in [("h1", 512), ("h2", 32)]:
        biases, rows = weights[name + "_bias"], weights[name + "_weight"]
        if len(biases) != hidden or len(rows) != hidden:
            raise ValueError("invalid affine dimensions")
        data.extend(integer_array("i", biases + [0] * (32 - hidden)))
        for row in rows:
            expected = 2 * width if name == "h1" else hidden
            if len(row) != expected:
                raise ValueError("invalid affine row")
            padded = ([*row[:width], *([0] * (256 - width)), *row[width:],
                       *([0] * (256 - width))] if name == "h1"
                      else row + [0] * (input_size - hidden))
            data.extend(integer_array("b", padded))
        data.extend(bytes((32 - hidden) * input_size))
    if len(weights["out_weight"]) != hidden:
        raise ValueError("invalid output dimensions")
    data.extend(struct.pack("<i", weights["out_bias"]))
    data.extend(integer_array("b", weights["out_weight"] + [0] * (32 - hidden)))
    if len(data) != network_size:
        raise ValueError("invalid serialized network size")
    return data


class integer_network:
    """Independent scalar evaluation of serialized bytes, including unused lanes."""

    def __init__(self, data):
        if (len(data) != network_size or struct.unpack_from("<III", data) != (0x7af32f16, 0x3e5aa6ee, 177)
                or struct.unpack_from("<I", data, transformer_start)[0] != 0x5d69d7b8
                or struct.unpack_from("<I", data, network_start)[0] != 0x63337156):
            raise ValueError("invalid legacy HalfKP bytes")
        self.ft = array("h")
        self.ft.frombytes(data[transformer_start + 4:network_start])
        if sys.byteorder != "little":
            self.ft.byteswap()
        offset = network_start + 4
        self.layers = []
        for size in [512, 32]:
            biases = struct.unpack_from("<32i", data, offset)
            offset += 128
            rows = []
            for row in range(32):
                values = struct.unpack_from(f"<{size}b", data, offset)
                offset += size
                rows.append([(i, w) for i, w in enumerate(values) if w])
            self.layers.append((biases, rows))
        self.out_bias = struct.unpack_from("<i", data, offset)[0]
        self.out_weight = struct.unpack_from("<32b", data, offset + 4)

    def evaluate(self, board):
        activations = []
        for color in [board.turn, not board.turn]:
            values = list(self.ft[:256])
            for index in features(board, color):
                offset = 256 + index * 256
                for lane in range(256):
                    value = values[lane] + self.ft[offset + lane]
                    values[lane] = (value + 32768) % 65536 - 32768
            activations.extend(min(127, max(0, value)) for value in values)
        for biases, rows in self.layers:
            activations = [min(127, max(0, (bias + sum(activations[i] * w for i, w in row)) // 64))
                           for bias, row in zip(biases, rows)]
        raw = divide_toward_zero(self.out_bias + sum(a * w for a, w in zip(activations, self.out_weight)), 16)
        return raw, max(-27999, min(27999, divide_toward_zero(raw * 100, 208)))
