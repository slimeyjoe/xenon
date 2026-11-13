"""Omake with all endings done: the CG gallery (K.CC) and the music mode (M.CC)."""


@scenario('omake-cg')
def omake_cg(ctx):
    g = ctx.game(progress=0x7F)
    g.until_blocked()
    g.choose_title(2)
    g.expect(g.title_rows == 2, 'omake menu')
    g.choose_title(0)
    g.expect(g.script_name == 'K', 'CG gallery script K running')
    g.play_until(lambda g: g.menu is not None)
    g.golden('omake-cg-menu')
    # walk the whole gallery: rotation visits every option of every menu once
    g.play_until(lambda g: g.title_rows is not None, max_steps=5000)
    gdts = {e.name.upper() for e in g.events_of('gdt') if e.a[0] == 0}
    g.expect(len(gdts) >= 40, 'gallery showed %d different images' % len(gdts))
    g.expect(g.last('error') is None, 'no script error in K.CC')


@scenario('omake-music')
def omake_music(ctx):
    g = ctx.game(progress=0x7F)
    g.until_blocked()
    g.choose_title(2)
    g.choose_title(1)
    g.expect(g.script_name == 'M', 'music mode script M running')
    g.play_until(lambda g: g.ok_wait)
    g.golden('omake-music')
    g.play_until(lambda g: g.title_rows is not None, max_steps=500)
    loads = [e.name for e in g.events_of('op') if e.a[0] == 0x0E]
    g.expect(len(loads) >= 10, 'music mode loaded %d songs' % len(loads))
