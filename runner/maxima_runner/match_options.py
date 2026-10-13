"""Validated match settings and identities for explicitly selected model files."""
import argparse
import hashlib
from pathlib import Path


# Shared match policy, including python-chess-managed variant/search options.
protected_options = {
    'hash', 'threads', 'multipv', 'ponder', 'wild', 'uci_variant', 'uci_chess960',
    'ownbook', 'drawcontempt', 'uci_analysemode', 'syzygyprobelimit', 'useexperience',
}


def option_assignment(text):
    name, separator, value = text.partition('=')
    name = name.strip()
    if not separator or not name or any(ord(char) < 32 or ord(char) == 127 for char in text):
        raise argparse.ArgumentTypeError('expected a single-line NAME=VALUE assignment')
    if name.lower() in protected_options:
        raise argparse.ArgumentTypeError(f'{name} is controlled by the shared match policy')
    return name, value


def file_identity(path):
    path = Path(path).resolve(strict=True)
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return {'path': str(path), 'sha256': digest.hexdigest()}


def engine_profile(options, models):
    requested = {}
    identities = {}
    seen = set()
    for name, value in options + models:
        key = name.lower()
        if key in seen:
            raise ValueError(f'duplicate engine option: {name}')
        seen.add(key)
        requested[name] = value
    for name, path in models:
        if not path:
            raise ValueError(f'model path is empty: {name}')
        identities[name] = file_identity(path)
        requested[name] = identities[name]['path']
    # These established network options must carry file identities too.
    for name in requested:
        if name.lower() in ('evalfile', 'evalfilesmall') and name not in identities:
            raise ValueError(f'use --model-a/b for {name} to record its file identity')
    return {'requested_options': requested, 'models': identities}


def verify_models(profile):
    for name, identity in profile['models'].items():
        if file_identity(identity['path']) != identity:
            raise ValueError(f'model file changed during match: {name}')


def validated_settings(advertised, settings, profile):
    """Validate the whole configuration before sending any setoption commands."""
    import chess.engine
    available = {name.lower(): option for name, option in advertised.items()}
    applied = {}
    values = {**settings, **profile['requested_options']}
    # Load weights before flags that enable an evaluator requiring those weights.
    model_names = {name.lower() for name in profile['models']}
    for name, value in sorted(values.items(), key=lambda item: item[0].lower() not in model_names):
        option = available.get(name.lower())
        if option is None:
            if name in profile['requested_options']:
                raise ValueError(f'engine does not advertise option: {name}')
            continue
        if name in profile['models'] and option.type != 'string':
            raise ValueError(f'model option must be a string: {name}')
        if option.type not in ('check', 'spin', 'combo', 'string'):
            raise ValueError(f'unsupported match option type: {name} ({option.type})')
        if option.type == 'check' and isinstance(value, str):
            if value.lower() not in ('true', 'false'):
                raise ValueError(f'expected true or false for {name}')
            value = value.lower() == 'true'
        try:
            applied[option.name] = option.parse(value)
        except chess.engine.EngineError as error:
            raise ValueError(str(error)) from error
    return applied
