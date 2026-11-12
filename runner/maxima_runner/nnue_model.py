"""Small CPU NNUE model; PyTorch is imported only by training."""
import torch
from torch import nn
from torch.nn import functional as functional

from .nnue_format import feature_count_for, output_scale


def rounded(value):
    return value.round().detach() + (value - value.detach())


def truncated(value):
    return value.trunc().detach() + (value - value.detach())


class halfkp_model(nn.Module):
    def __init__(self, width=8, hidden=8, architecture="halfkp"):
        super().__init__()
        self.architecture = architecture
        self.ft_weight = nn.Parameter(torch.empty(feature_count_for(architecture), width))
        self.ft_bias = nn.Parameter(torch.full((width,), 0.3))
        self.h1 = nn.Linear(width * 2, hidden)
        self.h2 = nn.Linear(hidden, hidden)
        self.out = nn.Linear(hidden, 1)
        nn.init.normal_(self.ft_weight, std=0.015)
        for layer in [self.h1, self.h2]:
            nn.init.uniform_(layer.weight, -0.2, 0.2)
            nn.init.constant_(layer.bias, 0.3)
        nn.init.uniform_(self.out.weight, -0.2, 0.2)
        nn.init.zeros_(self.out.bias)

    def forward(self, indices, offsets, quantized=True):
        quantize = rounded if quantized else lambda value: value
        values = functional.embedding_bag(indices, quantize(self.ft_weight * 127), offsets, mode="sum")
        values = (values + quantize(self.ft_bias * 127)).clamp(0, 127)
        values = values.reshape(-1, 2 * self.ft_weight.shape[1])
        for layer in [self.h1, self.h2]:
            values = functional.linear(values, quantize(layer.weight * 64), quantize(layer.bias * 8128)) / 64
            if quantized:
                values = values.floor().detach() + (values - values.detach())
            values = values.clamp(0, 127)
        values = functional.linear(values, quantize(self.out.weight * output_scale),
                                   quantize(self.out.bias * 13312)).flatten() / 16
        if quantized:
            values = truncated(values)
        values = values * 100 / 208
        return truncated(values) if quantized else values

    def constrain(self):
        with torch.no_grad():
            self.ft_weight.clamp_(-1, 1)
            self.ft_bias.clamp_(-1, 1)
            for layer in [self.h1, self.h2]:
                layer.weight.clamp_(-2, 127 / 64)
                layer.bias.clamp_(-16, 16)
            self.out.weight.clamp_(-128 / output_scale, 127 / output_scale)
            self.out.bias.clamp_(-16, 16)

    def integer_weights(self):
        def quantize(value, scale):
            return (value.detach() * scale).round().to(torch.int64).tolist()
        return {"ft_weight": quantize(self.ft_weight, 127), "ft_bias": quantize(self.ft_bias, 127),
                "h1_weight": quantize(self.h1.weight, 64), "h1_bias": quantize(self.h1.bias, 8128),
                "h2_weight": quantize(self.h2.weight, 64), "h2_bias": quantize(self.h2.bias, 8128),
                "out_weight": quantize(self.out.weight, output_scale)[0],
                "out_bias": quantize(self.out.bias, 13312)[0]}
