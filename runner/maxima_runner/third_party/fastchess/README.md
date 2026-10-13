# fast-chess logistic likelihood calculation

Python port of the logistic pentanomial branch of `SPRT::getLLR` and
`SPRT::getLLR_logistic` from Disservin/fast-chess, revision
`ebfd64cb7fac80d4822c50b07dca4fe934779d21`:
https://github.com/Disservin/fast-chess/blob/ebfd64cb7fac80d4822c50b07dca4fe934779d21/app/src/matchmaking/sprt/sprt.cpp

MIT license, copyright (c) 2023 disservin; license retained here.
The constrained multinomial maximum-likelihood estimator follows Michel Van den
Bergh's Proposition 1.1, cited upstream:
https://www.cantate.be/Fishtest/comparing_approximations.pdf

Changes: retain only the five-category logistic estimator, translated to Python;
replace upstream ITP root finding (tolerance1e-3) with monotone bisection
(tolerance2e-14,100iterations, bracket offset1e-9). Validate both MLE constraints
and raise on convergence failure. Zero-category regularization1e-3 is unchanged.
Project input validation, whole-pair aggregation, thresholds and reports live
outside this module. No SciPy/NumPy dependency at runtime. Normalized/Bayesian
models, Elo confidence estimates and overshoot corrections are not implemented.
Independent fixtures use pinned Fishtest's SciPy/Brent logistic likelihood.

To regenerate the committed test fixtures, fetch the pinned Fishtest
`server/fishtest/stats/LLRcalc.py` from revision
`7a337cdea6a9d941d6b12a0a86fd5a0d1f5104bc`, install SciPy1.15.3/NumPy2.2.6
in a separate development environment, and run:

```bash
python runner/tools/generate_sprt_reference.py --reference /path/to/LLRcalc.py \
    --output runner/tests/data/sprt_reference.json
```

The generator checks the reference source hash before importing it. Reference
code is not distributed with the runner; only independently computed numerical
fixtures and provenance are retained. These development dependencies are not
needed to run tests or matches.
