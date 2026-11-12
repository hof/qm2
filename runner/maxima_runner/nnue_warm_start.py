"""Approximate legacy HalfKP initialization for a full HalfKAv2_hm student."""
import hashlib

from . import nnue_format


def initialize_from_halfkp(torch, model, data):
    if (model.architecture != "halfkav2_hm" or model.ft_weight.shape != (22528, 256)
            or model.h1.weight.shape != (32, 512) or model.h2.weight.shape != (32, 32)):
        raise ValueError("HalfKP warm start requires HalfKAv2_hm with width256 and hidden32")
    teacher = nnue_format.integer_network(data)
    if teacher.architecture != "halfkp":
        raise ValueError("warm-start source must be a supported legacy HalfKP network")
    ft = torch.frombuffer(teacher.ft, dtype=torch.int16).reshape(-1, 256).to(model.ft_weight.dtype)
    destination, direct, reflected = [], [], []
    for bucket in range(32):
        king = (bucket // 4) * 8 + bucket % 4 + 4
        for piece in range(10):
            for square in range(64):
                destination.append(bucket * 704 + piece * 64 + square)
                # One row for FT bias, plus HalfKP's unused slot in each king bucket.
                direct.append(2 + king * 641 + piece * 64 + square)
                reflected.append(2 + (king ^ 7) * 641 + piece * 64 + (square ^ 7))
    projected = torch.zeros_like(model.ft_weight)
    projected[destination] = ((ft[direct] + ft[reflected]) / 2).round()
    # +/-508 per column and bias keeps any standard 32-piece sum int16-safe.
    if (ft[0].abs().max().item() > 508 or projected.abs().max().item() > 508
            or any(abs(value) > 16 * 8128 for biases, _ in teacher.layers for value in biases)
            or abs(teacher.out_bias) > 16 * 13312):
        raise ValueError("warm-start source exceeds supported training bounds")
    dense_weights = []
    for layer, (_, rows) in zip([model.h1, model.h2], teacher.layers):
        weights = torch.zeros_like(layer.weight)
        for row, entries in enumerate(rows):
            for column, value in entries:
                weights[row, column] = value / 64
        dense_weights.append(weights)
    # Validate and prepare everything before changing the student.
    with torch.no_grad():
        model.ft_weight.copy_(projected / 127)
        model.ft_bias.copy_(ft[0] / 127)
        for layer, weights, (biases, _) in zip([model.h1, model.h2], dense_weights, teacher.layers):
            layer.weight.copy_(weights)
            layer.bias.copy_(torch.tensor(biases, dtype=layer.bias.dtype) / 8128)
        model.out.weight.copy_(torch.tensor([teacher.out_weight], dtype=model.out.weight.dtype)
                               / nnue_format.output_scale)
        model.out.bias.fill_(teacher.out_bias / 13312)
    model.ft_bound = 4
    return {"method": "halfkp_mirror_average_v1", "source_sha256": hashlib.sha256(data).hexdigest(),
            "source_architecture": "halfkp", "ft_bound": model.ft_bound}
