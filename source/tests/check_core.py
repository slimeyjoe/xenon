#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The engine without a game's code, and the profile's checks. usage: check_core.py BIN PAK TMPDIR

The game's pack is rewritten with other profiles, and the program is run headless:
  - no native modules (modules= removed): the engine starts, shows no logo and comes to the
    title menu, and a new game runs the start script; an opening or staff roll would be a
    logged no-op;
  - a profile with a key the engine does not know, a required key missing, a module the
    program does not have: the engine refuses the pack with a clean error.
What the engine does with the game's modules is the subject of the other checks."""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'lrvne', 'tools'))
from lrvne import xpak   # noqa: E402


def repack(pak, out, edit):
    """the pack with its profile's text passed through edit(text)"""
    pack = xpak.read_pack(open(pak, 'rb').read())
    w = xpak.PackWriter(langs=[pack.langs[i] for i in sorted(pack.langs)])
    for e in pack.entries:
        if e.type == xpak.T_META and e.name == 'langs':
            continue
        data = e.data
        if e.type == xpak.T_META and e.name == 'profile' and not e.lang_tag:
            data = edit(data.decode('utf-8')).encode('utf-8')
        w.add(e.raw_name, e.type, data, lang=e.lang_tag or None)
    w.write(out)


def lines_of(text, drop):
    return '\n'.join(l for l in text.splitlines() if not l.startswith(drop + '=')) + '\n'


def run(binary, pak, tmp, spec, ticks):
    return subprocess.run([binary, '--pak', pak, '--storage', tmp, '--max-ticks', str(ticks),
                           '--control'], input=spec, capture_output=True, text=True, cwd=tmp)


def main():
    binary, pak, tmp = (os.path.abspath(a) for a in sys.argv[1:4])
    os.makedirs(tmp, exist_ok=True)
    fails = 0
    # no modules: the title, then a new game
    core = os.path.join(tmp, 'core.pak')
    repack(pak, core, lambda t: lines_of(t, 'modules'))
    r = run(binary, core, tmp, 'run\npress ok\nrun\nrun\nquit\n', 20000)
    events = [json.loads(l) for l in r.stdout.splitlines() if l.startswith('{')]
    kinds = [e['ev'] for e in events]
    if 'seq' in kinds:
        print('FAIL core: a sequence ran without its module')
        fails += 1
    if 'title_menu' not in kinds:
        print('FAIL core: no title menu (%s)' % r.stderr.strip()[-200:])
        fails += 1
    scripts = [e.get('name') for e in events if e['ev'] == 'script']
    if not scripts:
        print('FAIL core: the start script did not load')
        fails += 1
    # refused profiles
    for label, edit, want in (
            ('unknown key', lambda t: t + 'title_colour=3\n', 'title_colour'),
            ('missing key', lambda t: lines_of(t, 'title_image'), 'required'),
            ('unknown module', lambda t: lines_of(t, 'modules') + 'modules=no_such_module\n',
             'no_such_module'),
            ('bad value', lambda t: lines_of(t, 'title_rows') + 'title_rows=1,2\n',
             'title_rows')):
        bad = os.path.join(tmp, 'bad.pak')
        repack(pak, bad, edit)
        r = subprocess.run([binary, '--pak', bad, '--storage', tmp, '--max-ticks', '100'],
                           capture_output=True, text=True, cwd=tmp)
        if r.returncode == 0 or want not in r.stderr:
            print('FAIL %s: exit %d, %r' % (label, r.returncode, r.stderr.strip()[-200:]))
            fails += 1
    print('check_core: %d failures' % fails)
    return 1 if fails else 0


if __name__ == '__main__':
    sys.exit(main())
