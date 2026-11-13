"""SDL3 presentation: scale-mode geometry at several drawable sizes (plan §4.1), with the
rendered pixels compared to the planes at each (xe2e/present.py). Only for --backend sdl3."""

# (drawable, scale mode): 1:1, exact 2x, letterboxed 1080p, 4K (integer 5x, aspect 4x/5x),
# a drawable too small for one integer step, odd sizes for the filtered modes
CASES = [
    ((640, 400), 'integer'),
    ((1280, 800), 'integer'),
    ((1920, 1080), 'integer'),
    ((3840, 2160), 'integer'),
    ((1280, 800), 'aspect'),
    ((1920, 1080), 'aspect'),
    ((3840, 2160), 'aspect'),
    ((500, 300), 'integer'),
    ((1920, 1080), 'fit'),
    ((1000, 1000), 'fit'),
    ((1920, 1080), 'stretch'),
    ((1280, 800), 'stretch'),
]


@scenario('present-geometry', langs=('en',), backends=('sdl3',))
def present_geometry(ctx):
    for (w, h), mode in CASES:
        g = ctx.game('%dx%d-%s' % (w, h, mode), progress=0, window=(w, h), scale=mode)
        g.until_blocked()
        g.expect(g.title_rows == 3, 'title menu shown at %dx%d %s' % (w, h, mode))
        g.snapshot('title')                     # planes vs rendered, rectangle, border
        g.press('down')
        g.until_blocked()
        g.snapshot('title-load')
