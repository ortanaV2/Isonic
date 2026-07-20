# Isonic Developer

Isonic is an application for designing and simulating computer architectures.
It combines schematic capture with live logic simulation, letting you build
circuits - from single gates up to full CPUs - out of wires and ICs and watch
signals propagate in real time.

Beyond off-the-shelf parts, Isonic lets you emulate your own real ICs: define
a chip's pinout and behavior once, and place it like any other component.
Multi-layer routing with vias, a per-net diagnostics overlay (bus conflicts,
floating pins, fan-out warnings), and a way to program an EEPROM's contents
directly all make Isonic a schematic-design foundation for PCB development,
pairing schematic entry with the groundwork for PCB layout and routing in one
tool.

<img width="1278" height="829" alt="image" src="https://github.com/user-attachments/assets/022658c9-fbef-466e-829c-1f1f9258006a" />

## Installation

> No build tools required — just download and run. Windows only for now.

[Download latest Windows release](https://github.com/ortanaV2/Isonic/releases/download/v1.0.0/isonic-windows-x64-v1.0.0.zip)

<details>
<summary><strong>Manual download</strong></summary>

1. Go to [github.com/ortanaV2/Isonic/releases](https://github.com/ortanaV2/Isonic/releases)
2. Open the most recent release
3. Under **Assets**, download `isonic-windows-x64-<version>.zip`
4. Unzip it to any location
5. Run `isonic.exe`

</details>

## Features

- **Live logic simulation** — signals propagate across nets every frame; bus
  conflicts, overwritten drivers, floating pins, and fan-out overloads are
  flagged automatically by an on-canvas diagnostics overlay.
- **Multi-layer routing** — organize wiring across any number of named,
  colored layers (with GND/POWER roles), connected with vias where they need
  to cross.
- **Undo/redo** — every structural edit (wiring, placing, deleting, layer
  changes) is undoable, including via the mouse's back/forward side buttons.
- **File save/load** — schematics save to a versioned, human-readable
  `.isonic` file (layers, components, wiring, vias, and EEPROM contents all
  round-trip).
- **EEPROM programming** — an AT28C64B's memory can be viewed and edited
  directly, byte by byte, through a "Manage Data" panel, without wiring up
  its address/data/control pins.
- **Custom ICs** — new chips are added by defining their pinout and behavior
  once (see `src/ics/`); they then show up in the parts menu like any
  built-in part.
- **Configurable settings** — autosave interval, which corner the Layers
  panel docks to, and every tool/action keybind are rebindable from the
  Settings popup and persist across sessions.

## Building

Isonic targets Windows and requires a C11 compiler, SDL2, and SDL2_ttf
(`sdl2-config` must be on PATH) — MSYS2/mingw64 is the intended environment.

```
mingw32-make
```

Produces `isonic.exe`. `make run` builds and launches it, `make clean` removes
build artifacts.

## Files

Schematics save as `.isonic` files — a plain-text, versioned `key=value`
format that's safe to diff or read by hand. Settings (autosave interval,
Layers panel corner, keybinds) persist to `%APPDATA%\Isonic\settings.ini`,
independent of any schematic file.

## Controls

- **Left click** — tool-dependent: place/draw with the active tool, or
  select/drag in Select mode
- **Right click** — delete whatever's under the cursor
- **Middle mouse drag** — pan the camera
- **Mouse wheel** — zoom (centered on the cursor)
- **Mouse back/forward buttons** — undo/redo
- **W** — switch to the Wire tool
- **V** — switch to the Via tool
- **Space** — switch to Select mode
- **Q** — switch to the Input tool
- **E** — switch to the Output tool
- **1-9** — pick which layer new wires route on
- **Shift (hold)** — preview all layers at once; **Ctrl+Shift** toggles that
  preview locked on/off
- **Delete / Backspace** — delete the current selection
- **Escape** — cancel the current action, close an open popup, or drop the
  selection
- **Ctrl+C** — copy the selected component and start placing copies at the
  cursor
- **Ctrl+Z / Ctrl+Y** — undo / redo (**Ctrl+Shift+Z** also redoes)

All of the above (except the number keys, Delete/Backspace/Escape, and the
Shift/Ctrl+Shift layer-preview chord) can be rebound from the Settings popup.

Tools are also selectable from the taskbar at the top of the window (Select,
Wire, Via, Input, Output). The **Components** button opens a categorized,
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
- **Memory** — EEPROM (AT28C64B-15PU)

### Taskbar

- **File** — New Schematic, New Window, Open File, Save, Save As, Close
  Window (prompts to save unsaved changes first)
- **Settings** — autosave frequency, Layers panel corner, and keybind
  rebinding, with Save and Reset Default
- **Manage Data** — appears when a single AT28C64B is selected; opens a panel
  to view/edit its memory contents directly

### Layers panel

Docked below the taskbar (or centered on the left edge, if set in Settings):
lists every layer with its color, name, and position in the stack. Click a
row to make it the active layer for new wires, double-click a name to rename
it, use the reorder arrows or delete button, or add a new layer from the row
at the bottom.
