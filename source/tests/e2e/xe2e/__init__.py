"""End-to-end test harness for the Xenon engine.

The engine is driven through the null backend's control mode (lrvne/backends/control/control.c): the
backend reports engine events as JSON lines and stops whenever the engine waits for a key; the
driver answers with input. Three oracles check what came back:

  script.py    the event stream against the script bytecode (every message, menu, opcode)
  images.py    the CG area of the displayed frame against the independently decoded images
  timeline.py  frame timing: fades, effects, typing, ADT frames, the blink animator, waits

present.py   (SDL3) what the renderer drew against the planes
(The Amiga family's machines - WinUAE screenshots and guest-memory bitmaps, hosted AROS's
read-back pictures - are checked by lrvne's rigs: lrvne/rigs/lrvrig, used by ../amiga.py and
../aros.py.)

golden.py keeps human-approved snapshots (4-bit PNG) for what has no independent oracle.
game.py ties it together for the scenarios; run.py runs them.
"""
