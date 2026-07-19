# Isonic

Isonic is an application for designing and simulating computer architectures.
It combines schematic capture with live logic simulation, letting you build
circuits — from single gates up to full CPUs — out of wires and ICs and watch
signals propagate in real time.

Beyond off-the-shelf parts, Isonic lets you emulate your own real ICs: define
a chip's pinout and behavior once, and place it like any other component.
This also makes Isonic the schematic-design foundation for PCB development —
pairing schematic entry with the groundwork for PCB layout and routing in one
tool.

<img width="1278" height="829" alt="image" src="https://github.com/user-attachments/assets/022658c9-fbef-466e-829c-1f1f9258006a" />

## Building

Requires a C11 compiler, SDL2, and SDL2_ttf (`sdl2-config` must be on PATH).

```
mingw32-make
```

Produces `isonic.exe`. `make run` builds and launches it, `make clean` removes
build artifacts.

## Controls

- **Left click** — tool-dependent: place/draw with the active tool, or
  select/drag in Select mode
- **Right click** — delete whatever's under the cursor
- **Middle mouse drag** — pan the camera
- **Mouse wheel** — zoom (centered on the cursor)
- **W** — switch to the Wire tool
- **Space** — switch to Select mode
- **Delete / Backspace** — delete the current selection
- **Escape** — cancel the current action and drop the selection
- **Ctrl+C** — copy the selected component and start placing copies at the cursor

Tools are also selectable from the taskbar at the top of the window (Select,
Wire, Input, Output). The **Components** button opens a categorized,
fold-up parts menu — click a category to expand it, then click a part to
start placing it:

- **Logic Gates** — AND (SN74HC08N), OR (SN74HC32N), NOT (CD74HC04E),
  NAND (SN74HC00N), NOR (SN74HC02N), XOR (SN74HC86N)
- **Multiplexers** — 8:1 MUX (SN74HC151N), Dual 4:1 MUX (SN74HC153N)
- **Demultiplexers** — 1:8 DEMUX (CD74HCT238E), Dual 1:4 DEMUX (CD4555BE)
- **Buffers** — Tri-State Buffer (SN74HC244N)
- **Latches** — D-Latch (TC74HC373APF)
- **Counters** — 12-Bit Binary Counter (SN74HC4040N)
- **Timers** — Timer (TLC555)
