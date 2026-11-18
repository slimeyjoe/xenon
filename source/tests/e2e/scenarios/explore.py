"""Random walks: no goldens, only the oracles. Each walk starts from a progress state, picks
menu options at random, sometimes opens the system menu and cancels it, and plays until the
engine returns to the title. Reports page coverage."""
import os
import random
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from xe2e.game import Rotation   # noqa: E402

STARTS = [0x00, 0x01, 0x03, 0x07, 0x7F]


def walk(ctx, seed, progress, max_steps=20000):
    rnd = random.Random(seed)
    g = ctx.game('seed%d' % seed, progress=progress)
    g.until_blocked()
    g.choose_title(rnd.choice([0, 0, 0, 1, 2]))
    if g.title_rows == 2:                        # omake menu (locked or not)
        g.choose_title(rnd.randrange(2))
        if g.ok_wait:
            g.advance()
            g.cancel()
            g.choose_title(0)
    if g.script_name == 'L' and g.menu:
        g.choose(len(g.menu[0]) - 1)
        g.choose_title(0)
    cancels = [0]
    rotation = Rotation()

    def policy(game, items):
        if rnd.random() < 0.03 and cancels[0] < 3:
            cancels[0] += 1
            game.cancel()
            if game.menu and game.menu_system:
                game.choose(0)
            return rnd.randrange(len(game.menu[0])) if game.menu else 0
        # mostly the flag-aware rotation (it gets through the quiz loops), sometimes random
        if rnd.random() < 0.3:
            return rnd.randrange(len(items))
        return rotation(game, items)

    g.play_until(lambda g: g.title_rows is not None and g.events_of('script'), policy=policy,
                 max_steps=max_steps)
    visited = len(g.script.visited)
    g.expect(visited >= 3, 'walk %d visited %d pages' % (seed, visited))
    return g


@scenario('explore', tags=('slow',), parts=3)
def explore(ctx):
    i = ctx.part                                 # the three walks run in parallel
    seed = ctx.seed * 100 + i
    walk(ctx, seed, STARTS[(seed + i) % len(STARTS)])
