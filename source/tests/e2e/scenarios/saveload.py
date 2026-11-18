"""Save at the first save point of DREAM_01, load it from the title in a fresh engine."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
from xe2e.game import read_save   # noqa: E402


@scenario('save-load')
def save_load(ctx):
    g = ctx.game('save', progress=0)
    g.until_blocked()
    g.choose_title(0)
    # the rotation policy takes "save" then slot 1 at the first save menu
    g.play_until(lambda g: g.last('save') is not None)
    ev = g.last('save')
    path = os.path.join(g.storage, ev.name)
    g.expect(os.path.exists(path), 'save file %s written' % path)
    g.play_until(lambda g: g.ok_wait)               # the game goes on after saving
    sv = read_save(path)
    g.expect(sv['script'].upper() == g.script_name, 'save names the running script (%s)' % sv['script'])
    g.golden('after-save')

    h = ctx.game('load', progress=0, saves={ev.name: path})
    h.until_blocked()
    h.choose_title(1)
    h.expect(h.script_name == 'L' and h.menu is not None, 'load menu')
    h.choose(int(ev.name[-1]) - 1)
    h.play_until(lambda h: h.script_name != 'L')
    h.expect(h.script_name == sv['script'].upper(), 'resumed in %s' % sv['script'])
    h.expect(h.page == sv['page'], 'resumed at page %d (got %s)' % (sv['page'], h.page))
    loaded = h.last('load')
    h.expect(loaded is not None and loaded.a[0] == 1, 'LOAD reported success')
    if sv['image']:
        h.expect(h.last('gdt', name=sv['image']) is not None, 'saved image %s restored' % sv['image'])
    h.play_until(lambda h: h.ok_wait)
    h.golden('after-load')
    # the resumed game must continue like the original run: same next script
    h.play_until(lambda h: h.script_name != sv['script'].upper(), max_steps=3000)
    g.play_until(lambda g: g.script_name != sv['script'].upper(), max_steps=3000)
    g.expect(h.script_name == g.script_name, 'both runs continue into %s / %s' % (g.script_name, h.script_name))
