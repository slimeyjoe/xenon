"""Logo, title menu, omake lock, load menu, quit."""


@scenario('logo')
def logo(ctx):
    g = ctx.game(progress=0)
    g.eng.breaks('wait')
    # the logo sequence: fade-ins, a 120-tick hold with the full logo, fade to black
    hold = None
    for _ in range(200):
        stop = g.run()
        if stop.ev == 'exit':
            g.fail('engine exited during the logo')
            return
        hold = g.last('wait', a0=0, a1=0, a2=120)
        if hold or g.last('seq', a0=0, a1=1):
            break
    g.expect(hold is not None, '120-tick hold inside the logo sequence')
    g.golden('logo')
    g.eng.breaks(clear=True)
    stop = g.until_blocked()
    end = g.last('seq', a0=0, a1=1)
    g.expect(end is not None, 'logo sequence end event')
    g.expect(g.title_rows == 3, 'title menu with 3 rows after the logo')
    g.expect(stop.t - end.t >= 120, 'logo held for at least 120 ticks before the title')


@scenario('title')
def title(ctx):
    g = ctx.game(progress=0)
    g.until_blocked()
    g.expect(g.title_rows == 3, 'title menu shown')
    g.golden('title')
    g.press('down')
    g.until_blocked()
    g.golden('title-load')
    g.press('down')
    g.until_blocked()
    g.press('down')                                     # no wrap: stays on the last row
    g.until_blocked()
    g.golden('title-omake')

    # omake while locked: MENU2, pick CG -> notice message, OK -> MENU2 again, cancel -> title
    g.press('ok')
    g.until_blocked()
    g.expect(g.title_rows == 2, 'omake menu has 2 rows')
    g.golden('omake-menu')
    g.press('ok')
    g.until_blocked()
    g.expect(g.ok_wait and g.last('msg') is not None and g.last('msg').pos is None,
             'locked-omake notice is an engine message')
    g.golden('omake-locked')
    g.press('ok')
    g.until_blocked()
    g.expect(g.title_rows == 2, 'back to the omake menu')
    g.press('cancel')
    g.until_blocked()
    g.expect(g.title_rows == 3, 'cancel returns to the title menu')

    # Load: L.CC page 0 (load / back), page 1 (4 slots); an empty slot shows the no-data box
    # and the slot menu again; "back to the menu" EXITs to the title
    g.choose_title(1)
    g.expect(g.script_name == 'L' and g.menu is not None and len(g.menu[0]) == 2, 'L.CC first menu shown')
    g.golden('load-menu')
    g.choose(0)
    g.expect(g.menu is not None and len(g.menu[0]) == 4, 'slot menu shown')
    g.golden('load-slots')
    g.choose(0)
    g.expect(g.box is not None, 'no-data box shown for an empty slot')
    g.golden('load-nodata')
    g.press('ok')
    g.until_blocked()
    g.expect(g.menu is not None and len(g.menu[0]) == 4, 'slot menu again after the box')
    g.cancel()
    g.expect(g.menu is not None and g.menu_system, 'system menu from the slot menu')
    g.choose(1)
    g.expect(g.title_rows == 3, 'return to title from the system menu')
    g.choose_title(1)
    g.choose(1)
    g.expect(g.title_rows == 3, 'EXIT from L.CC returns to the title')

    # window close
    g.press('quit')
    stop = g.run()
    g.expect(stop.ev == 'exit' and stop.raw.get('code') == 0, 'engine exits cleanly on quit')


@scenario('sysmenu')
def sysmenu(ctx):
    """Cancel at a script menu opens the system menu; its options work."""
    g = ctx.game(progress=0)
    g.until_blocked()
    g.choose_title(0)
    g.play_until(lambda g: g.menu is not None and not g.menu_system)
    first = (g.script_name, g.page)
    g.golden('s00-menu')
    g.cancel()
    g.expect(g.menu is not None and g.menu_system, 'system menu shown on cancel')
    g.golden('sysmenu')
    g.choose(0)                                         # cancel: back to the script menu
    g.expect(g.menu is not None and not g.menu_system, 'script menu shown again')
    g.cancel()
    g.choose(2)                                         # quit -> confirmation
    g.expect(g.menu is not None and g.menu_system and len(g.menu[0]) == 2, 'quit confirmation')
    g.golden('sysmenu-quit')
    g.choose(0)                                         # continue
    g.expect(g.menu is not None and not g.menu_system, 'continue returns to the script menu')
    g.cancel()
    g.choose(1)                                         # return to title
    g.expect(g.title_rows == 3, 'return to title from the system menu')
    g.expect((g.script_name, g.page) == first, 'no script ran after the system menu')
