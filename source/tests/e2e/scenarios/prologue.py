"""The prologue S00: title cards, the first room, the first choice; typing and CTRL skip."""


@scenario('prologue')
def prologue(ctx):
    g = ctx.game(progress=0)
    g.until_blocked()
    g.choose_title(0)
    g.expect(g.script_name == 'S00', 'Start on a fresh game runs S00')
    g.golden('s00-first-wait')
    # play to the first image shown in the room, then to the first choice
    g.play_until(lambda g: g.last('gdt', name='0010') is not None and g.last('gdisp') is not None
                 and g.last('effect', a1=1) is not None and g.ok_wait)
    g.golden('s00-room')
    g.play_until(lambda g: g.menu is not None)
    g.expect(g.page is not None, 'in a page of S00')
    g.golden('s00-choice')
    g.choose(0)
    g.play_until(lambda g: g.script_name == 'S0101')
    g.expect(g.script.visited and ('S00', 1) in g.script.visited, 'S00 page 1 visited')
    g.expect(len([e for e in g.events_of('msg') if e.pos is not None]) >= 100,
             'at least 100 script messages shown in S00')


@scenario('instant-text')
def instant_text(ctx):
    """KANA-lock equivalent: messages appear without the per-glyph wait."""
    g = ctx.game(progress=0)
    g.instant_text(True)
    g.until_blocked()
    g.choose_title(0)
    g.play_until(lambda g: g.menu is not None)
    msgs = [e for e in g.events_of('msg') if e.pos is not None]
    g.expect(all(e.a[0] == 1 for e in msgs), 'every message printed with the instant flag')
    g.golden('s00-choice-instant')


@scenario('ctrl-skip')
def ctrl_skip(ctx):
    """CTRL held: every wait ends at once, the prologue races to its menu."""
    g = ctx.game(progress=0)
    g.until_blocked()
    g.hold_ctrl(True)
    g.choose_title(0)
    g.play_until(lambda g: g.menu is not None)
    t_menu = g.tick
    g.hold_ctrl(False)
    waits = [e for e in g.events_of('wait') if e.a[1] == 1]
    skipped = [e for e in waits if e.a[2] == 1]
    g.expect(len(skipped) > 10, 'waits were cut short by CTRL (%d of %d)' % (len(skipped), len(waits)))
    # wait_vsync still spends one frame per call with CTRL held (platform.md §4.1), so the
    # typing costs one tick per full-width glyph; the Japanese prologue has many more of them
    g.expect(t_menu < 4000, 'reached the first choice in %d ticks with CTRL held' % t_menu)
    g.choose(0)
    g.play_until(lambda g: g.ok_wait)
    g.expect(g.ok_wait, 'key waits block again once CTRL is released')


@scenario('quit-in-story')
def quit_in_story(ctx):
    """A quit (window close / Ctrl-C) or Shift+Esc in the middle of a script ends the game at
    once: at a key wait, and between two ticks of a fade or a timed wait. The engine used to
    play on to the script's next menu (up to ~90 s of the prologue, and as long on an
    Amiga)."""
    for key, budget in (('quit', 0), ('emergency', 0), ('quit', 7), ('emergency', 23)):
        g = ctx.game('%s-%d' % (key, budget), progress=0)
        g.until_blocked()
        g.choose_title(0)
        g.advance(2)
        if budget:
            g.press('ok')
            g.run(budget)                               # stopped inside a fade or a wait
        t0 = g.tick
        g.press(key)
        stop = g.run()
        quit_ev = g.last('quit')
        g.expect(stop.ev == 'exit' and stop.raw.get('code') == 0,
                 '%s: engine exits cleanly' % key)
        g.expect(quit_ev is not None and quit_ev.t - t0 <= 1,
                 '%s at t=%d: engine left at t=%s' % (key, t0, quit_ev.t if quit_ev else None))
