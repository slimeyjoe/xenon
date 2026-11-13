"""The test rigs' machine settings for the Python harness (rig.conf.example, rig.sh): the value
of KEY from the environment, else from tests/rig.conf (or $RIG_CONF), else the default."""
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def _load():
    path = os.environ.get('RIG_CONF') or os.path.join(HERE, 'rig.conf')
    out = {}
    try:
        with open(path, encoding='utf-8') as f:
            for line in f:
                line = line.rstrip('\n')
                if not line or line.startswith('#') or '=' not in line:
                    continue
                k, v = line.split('=', 1)
                out[k] = os.path.expanduser(v) if v.startswith('~/') else v
    except OSError:
        pass
    return out


_CONF = _load()


def get(key, default=None):
    v = os.environ.get(key) or _CONF.get(key)
    return v if v else default
