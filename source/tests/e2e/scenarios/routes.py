"""Whole-game routes to the seven endings (docs/re/script-catalog.md §2, §3).

A route is a progress byte to start from and rules for the menus that decide the branch,
keyed by (script, page): a label id to take, or ('not', id) to avoid. Every other menu is
played by rotation (each option once, in order), which also exercises the save menus.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from xe2e.game import Rotation   # noqa: E402

ROUTES = {
    # name: (progress, rules, expected scripts in order, ending bit)
    'end1': (0x00, {}, ['S00', 'S0101', 'S0102', 'S0103', 'S0104', 'S0105', 'S0106', 'S0107',
                        'S0108', 'S0109', 'S0110', 'S0111'], 1),
    'end2': (0x01, {('S00B', 6): 2}, ['S00B', 'S0201', 'S0202', 'S0203', 'S0204', 'S0205', 'S0206', 'S0207'], 2),
    'end3-alpha': (0x01, {('S00B', 6): 1, ('S0301', 1): ('not', 1), ('S0301', 5): 1, ('S0302A', 3): ('not', 1)},
                   ['S00B', 'S0301', 'S0301A', 'S0302A', 'S0303A', 'S0304A', 'S0305A', 'S0306A'], 3),
    'end4-beta': (0x01, {('S00B', 6): 1, ('S0301', 1): ('not', 1), ('S0301', 5): 1, ('S0302A', 3): 1,
                         ('S0304A', 3): ('not', 1)},
                  ['S00B', 'S0301', 'S0301A', 'S0302A', 'S0303A', 'S0304A', 'S0305A', 'S0306A'], 4),
    'end5-gamma': (0x01, {('S00B', 6): 1, ('S0301', 1): ('not', 1), ('S0301', 5): 2, ('S0303B', 3): 1,
                          ('S0305B', 3): ('not', 1)},
                   ['S00B', 'S0301', 'S0302B', 'S0303B', 'S0304B', 'S0305B', 'S0306B', 'S0306A'], 5),
    'end6-delta': (0x01, {('S00B', 6): 1, ('S0301', 1): ('not', 1), ('S0301', 5): 1, ('S0302A', 3): 1,
                          ('S0304A', 3): 1},
                   ['S00B', 'S0301', 'S0301A', 'S0302A', 'S0303A', 'S0304A', 'S0305A', 'S0306A'], 6),
    'end7': (0x07, {('S00C', 5): 1}, ['S00C', 'S0401', 'S0402', 'S0403'], 7),
    'detour-b': (0x01, {('S00B', 6): 1, ('S0301', 1): 1, ('S0303B', 3): ('not', 1)},
                 ['S00B', 'S0301', 'S0301B', 'S0302B', 'S0303B', 'S0304B', 'S0305B', 'S0306B', 'S0306A'], 3),
}


def make_policy(g, rules):
    rotation = Rotation()

    def policy(game, items):
        vis = game.script.visible
        rule = rules.get((game.script_name, game.page))
        if rule is not None and vis and len(vis) == len(items):
            ids = [o['id'] for o in vis]
            if isinstance(rule, tuple):
                for k, i in enumerate(ids):
                    if i != rule[1]:
                        return k
            elif rule in ids:
                return ids.index(rule)
        return rotation(game, items)
    return policy


def play_route(ctx, name):
    progress, rules, chain, bit = ROUTES[name]
    g = ctx.game(progress=progress)
    g.until_blocked()
    g.choose_title(0)
    policy = make_policy(g, rules)
    g.play_until(lambda g: g.title_rows is not None and g.last('progress') is not None, policy=policy)
    done = g.last('progress')
    g.expect(done is not None and done.a[0] == bit, 'COMPLETE %d reached (got %s)' % (bit, done.a[0] if done else None))
    g.expect(g.last('seq', a0=2, a1=1) is not None, 'staff roll played')
    order = []
    for e in g.events_of('script'):
        if e.a[0] == -1 and (not order or order[-1] != e.name.upper()):
            order.append(e.name.upper())
    g.expect(order == chain, 'script chain %s (expected %s)' % (order, chain))
    p = open(os.path.join(g.storage, 'progress.dat'), 'rb').read()
    g.expect(p == bytes([progress | (1 << (bit - 1))]), 'progress byte %r after the ending' % p)
    saves = [e for e in g.events_of('save')]
    for e in saves:
        path = os.path.join(g.storage, e.name + '.dat')
        g.expect(os.path.exists(path), 'save %s written' % e.name)
    return g


for _name in ROUTES:
    def _make(n):
        def fn(ctx):
            play_route(ctx, n)
        return fn
    scenario('route-' + _name, tags=('route',))(_make(_name))
