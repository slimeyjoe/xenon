# X11 bitmap fonts

The bitmap fonts `make pack` turns into the engine's glyph tables (`tools/xfont.py`; how each one
is used: `../../lang/README.md` "Fonts"). They are the BDF sources, unmodified, from the X.Org
releases:

| File           | From                                  | Licence                               |
|----------------|---------------------------------------|---------------------------------------|
| `8x16rk.bdf`   | `font-sony-misc-1.0.4.tar.xz`         | Sony, notice kept: `COPYING.sony-misc` |
| `8x16.bdf`     | `font-sony-misc-1.0.4.tar.xz`         | Sony, notice kept: `COPYING.sony-misc` |
| `jiskan16.bdf` | `font-jis-misc-1.0.4.tar.xz`          | JIS X 9051-1984, by permission to use: `COPYING.jis-misc` |

Neither is public domain. The Sony fonts may be used, modified and distributed freely provided
the copyright and permission notice appear in all copies and in the supporting documentation (the
notice is also in each BDF header). `jiskan16` carries only the statement in its COPYING file and
its `COPYRIGHT` property; X.Org and the Linux distributions ship it as free.

Downloaded from https://www.x.org/releases/individual/font/ on 2026-11-12. Both tarballs carry a
good signature by the X.Org release manager, Alan Coopersmith (RSA key CFDF148828C642A7, primary
key A2FB9E081F2D130E). SHA-256:

    e6b09f823fccb06e0bd0b2062283b6514153323bd8a7486e9c2e3f55ab84946b  font-sony-misc-1.0.4.tar.xz
    78d1eff6c471f7aa6802a26d62cccf51d8e5185586406d9b6e1ee691b0bffad0  font-jis-misc-1.0.4.tar.xz
    497f1cc5511023b636e6798886a7e43991fa8977a16c01d556e5f18f4ac2fd84  8x16.bdf
    b935021e6f03dce8ccd83ff50efd1723ca2885275c46c02534e8448942afb6e1  8x16rk.bdf
    e9ee1273ce9d6675e8f4a24e49e0d89ec49f003f464d22828af48915dd8f6dbf  jiskan16.bdf

The glyph tables made from these files are byte-identical to the ones made from the compiled
`.pcf.gz` fonts of the distributions (checked against MacPorts' `font-sony-misc` and
`font-jis-misc`). `make pack X11FONTS=DIR` takes the BDFs from another directory.
