"""End-to-end test harness for the Xenon engine.

The engine is driven through the null backend's control mode (backends/null/control.c): the
backend reports engine events as JSON lines and stops whenever the engine waits for a key; the
driver answers with input. Three oracles check what came back:

  script.py    the event stream against the script bytecode (every message, menu, opcode)
  images.py    the CG area of the displayed frame against the independently decoded images
  timeline.py  frame timing: fades, effects, typing, ADT frames, the blink animator, waits

present.py   (SDL3) what the renderer drew against the planes
amiga_oracle.py, uae.py  (Amiga in WinUAE, ../amiga.py) screenshots and guest-memory bitmaps
             against the null backend's frames

golden.py keeps human-approved snapshots (4-bit PNG) for what has no independent oracle.
game.py ties it together for the scenarios; run.py runs them.
"""
