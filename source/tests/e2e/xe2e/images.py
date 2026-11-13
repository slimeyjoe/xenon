"""Oracle B: the CG area of the displayed frame against the independently decoded images.

re-tools/gdt.py decodes GDT files byte-exactly (verified against the original decoder under
emulation). This oracle replays the engine's buffer operations with it - G_LOAD and the diff /
buffer loads decode into the off-screen buffers, G_PARTS_COPY merges a bust, G_CLEAR wipes - and
whenever the engine shows the buffers (G_DISP or the end of an effect) it remembers what the
display rect must now contain. A check compares a frame against that expectation, leaving out
the regions other things draw into: the text window, the choice menu, the blink animator strip,
the title-menu highlight, the lower-screen clear. Anything the model does not follow (ADT
playback, scrolling, page switching, the built-in sequences) invalidates the expectation until
the next G_DISP.
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', 'tools'))
sys.path.insert(0, os.path.join(HERE, '..', '..', '..', '..', 're-tools'))
import gdt     # noqa: E402
import xpak    # noqa: E402

CG_RECT = (5, 4, 70, 296)        # xb, y, wb, h of the window's CG area (waku2 close)


class ImageOracle:
    def __init__(self, pak_path):
        entries = xpak.read(open(pak_path, 'rb').read())
        self.gdts = {e['name'].upper(): e['data'] for e in entries if e['type'] == xpak.T_GDT}
        self.problems = []
        self.checks = 0
        self.reset()

    def reset(self):
        self.gbuf = gdt.new_planes()
        self.rect = None                 # (xb, y, wb, h) display rect of the last G_LOAD
        self.parts = None
        self.expect = None               # (rect, index array of that rect) or None
        self.text_win = (10, 313)        # (xb, y) of the text window (engine default)
        self.menu = (10, 313)            # (xb, y)
        self.display_page = 0
        self.blink_rect = None           # (xb, y, wb, h)
        self.blink_active = False
        self.lower_cleared = False
        self.title_bar = False
        self.last_name = None

    # ---- events ----
    def on_event(self, ev):
        k = ev.ev
        if k == 'gdt':
            self._gdt(ev)
        elif k == 'gdisp':
            off, wb, h = ev.a[0], ev.a[1], ev.a[2]
            self._shown((off % 80, off // 80, wb, h))
        elif k == 'effect' and ev.a[1] == 1:
            kind = ev.a[0]
            if kind in (0, 1) and self.rect:
                # Both wipes copy 8 rows per step for h/8 steps (0C2A:0690, 0C2A:075E), so
                # with h not a multiple of 8 the rows outside the covered band are never
                # shown and keep whatever was on screen; expect only the covered rows.
                xb, y, wb, h = self.rect
                steps = h // 8
                if kind == 0:
                    self._shown((xb, y, wb, 8 * steps))
                else:
                    mid = h // 2 - 1
                    top = mid - 4 * steps + 1
                    self._shown((xb, y + top, wb, 8 * steps))
            elif kind == 2:
                self._shown(CG_RECT)
            else:
                self.expect = None
        elif k == 'op':
            self._op(ev)
        elif k == 'adt' and ev.a[0] == -2:
            self.expect = None
        elif k == 'seq':
            self.expect = None
        elif k == 'blink':
            if ev.a[0] == -1:
                self.blink_active = True
            elif ev.a[0] == -2:
                self.blink_active = False
        elif k == 'title_menu':
            self.title_bar = True
        elif k == 'clear':
            if ev.a[0] == 1:
                self.gbuf = gdt.new_planes()
            elif ev.a[1] == self.display_page:
                self.expect = None
        elif k == 'load' and ev.a[0]:
            pass                                 # the engine follows with gdt + gdisp events

    def _gdt(self, ev):
        name = (ev.name or '').upper()
        data = self.gdts.get(name)
        if data is None or not ev.a[1]:
            return
        kind = ev.a[0]
        try:
            hdr = gdt.parse_header(data)
        except gdt.GdtError as e:
            self.problems.append('%s: header: %s' % (name, e))
            return
        if kind == 3:
            planes = gdt.new_planes()
            gdt.decode(data, planes, strict=False)
            self.parts = (planes, hdr['xb'], hdr['y'], hdr['wb'], hdr['h'])
            return
        gdt.decode(data, self.gbuf, strict=False)
        if kind == 0:
            self.rect = (hdr['xb'], hdr['y'], hdr['wb'], hdr['h'])
            self.last_name = name

    def _op(self, ev):
        op = ev.a[0]
        ints = ev.ints or []
        if op == 0x23:                                    # G_CLEAR
            self.gbuf = gdt.new_planes()
        elif op == 0x35 and self.parts:                   # G_PARTS_COPY
            planes, xb, y, wb, h = self.parts
            gdt.merge_color8(self.gbuf, planes, xb, y, wb, h)
        elif op == 0x38 and len(ints) >= 4:               # TEXTPOS
            self.text_win = (ints[0], ints[1])
            self.menu = (ints[2], ints[3])
        elif op == 0x2E and len(ints) >= 6:               # ANIM_START
            self.blink_rect = (ints[2], ints[3], ints[4], ints[5])
        elif op == 0x03:                                  # CLEAR_LOWER
            self.lower_cleared = True
        elif op == 0x44 and ints:                         # VRAM_DPAGE
            self.display_page = 1 if ints[0] else 0
            self.expect = None
        elif op in (0x11, 0x32, 0x43, 0x29, 0x47, 0x2D):
            self.expect = None

    def _shown(self, rect):
        xb, y, wb, h = rect
        idx = gdt.planes_to_index(self.gbuf)
        self.expect = (rect, idx[y:y + h, xb * 8:(xb + wb) * 8].copy())
        self.lower_cleared = False
        self.title_bar = False
        if not self.blink_active:
            self.blink_rect = None

    # ---- check ----
    def mask(self):
        """True where the frame may differ from the expectation (other drawers)."""
        m = np.zeros((400, 640), bool)
        if self.text_win:
            xb, y = self.text_win
            m[y:y + 72, xb * 8:(xb + 62) * 8] = True
        if self.menu:
            xb, y = self.menu
            m[y:y + 9 * 18, xb * 8:(xb + 60) * 8] = True
        if self.blink_rect:
            xb, y, wb, h = self.blink_rect
            m[y:y + h, xb * 8:(xb + wb) * 8] = True
        if self.lower_cleared:
            m[296:, :] = True
        if self.title_bar:
            m[176:304, 26 * 8:54 * 8] = True
        return m

    def check(self, frame, where=''):
        """Compare frame.idx with the expectation. Returns a problem string or None."""
        if self.expect is None:
            return None
        (xb, y, wb, h), want = self.expect
        self.checks += 1
        got = frame.idx[y:y + h, xb * 8:(xb + wb) * 8]
        ok = ~self.mask()[y:y + h, xb * 8:(xb + wb) * 8]
        diff = (got != want) & ok
        if diff.any():
            ys, xs = np.nonzero(diff)
            msg = ('%s: CG area of %s differs from the decoded image in %d pixels '
                   '(x %d-%d, y %d-%d)' % (where, self.last_name, int(diff.sum()),
                                           xb * 8 + xs.min(), xb * 8 + xs.max(), y + ys.min(), y + ys.max()))
            self.problems.append(msg)
            return msg
        return None
