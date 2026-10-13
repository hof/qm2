"""Generate fixtures from pinned Fishtest LLRcalc.py in a SciPy development environment."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import random

import numpy
import scipy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    source_hash = hashlib.sha256(args.reference.read_bytes()).hexdigest()
    if source_hash != '6e2cc30a55da03233e82d4ef97f1aa50118ce6732d11d8d3a756a656f5eecb94':
        raise SystemExit('reference does not match pinned Fishtest revision')
    spec = importlib.util.spec_from_file_location('reference_llr', args.reference)
    reference = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(reference)
    counts_list = [
        [2, 11, 13, 15, 9], [0, 0, 100, 0, 0], [1, 0, 0, 0, 0], [0, 0, 0, 0, 1],
        [1000, 0, 0, 0, 0], [0, 0, 0, 0, 1000], [223, 9863, 21279, 10037, 246],
        [871, 26175, 55983, 26678, 821], [1000000] * 5,
    ]
    rng = random.Random(20261014)
    counts_list += [[rng.randrange(10000) if rng.random() > .3 else 0 for _ in range(5)]
                    for _ in range(30)]
    cases = []
    for counts in counts_list:
        for bounds in [[0, 5], [0, 10], [-5, 0], [-5, 5], [.5, 2.5], [-200, 200]]:
            cases.append({'counts': counts, 'bounds': bounds,
                          'llr': reference.LLR_logistic(*bounds, counts)})
    metadata = {
        'reference': 'official-stockfish/fishtest LLRcalc.LLR_logistic',
        'revision': '7a337cdea6a9d941d6b12a0a86fd5a0d1f5104bc',
        'source_sha256': source_hash, 'scipy_version': scipy.__version__,
        'numpy_version': numpy.__version__, 'seed': 20261014,
    }
    args.output.write_text(json.dumps(metadata, indent=2)[:-2] + ',\n  "cases": [\n' +
                           ',\n'.join('    ' + json.dumps(case) for case in cases) + '\n  ]\n}\n')
    print('reference cases', len(cases))


if __name__ == '__main__':
    main()
