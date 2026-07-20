<img width="1200" height="310" alt="Isonic_Banner" src="https://github.com/user-attachments/assets/086de0b7-fbd9-4f60-8ea4-4dc9483403e3" />

# Isonic: Computer Architecture Development Software

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

## [Download latest Windows release](https://github.com/ortanaV2/Isonic/releases/download/v1.0.0/isonic-windows-x64-v1.0.0.zip)
<details>
<summary><strong>Manual download</strong></summary>

1. Go to [github.com/ortanaV2/Isonic/releases](https://github.com/ortanaV2/Isonic/releases)
2. Open the most recent release
3. Under **Assets**, download `isonic-windows-x64-<version>.zip`
4. Unzip it to any location
5. Run `isonic.exe`

</details>

<img width="1277" height="828" alt="image" src="https://github.com/user-attachments/assets/03351805-abea-45f9-8e37-3f78c176be3c" />

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
- **Annotations** — Section-Labeling draws a labeled, resizable, lockable
  rectangle to frame off part of a schematic; Text Label drops a single
  freestanding line of text — both purely organizational, with no effect on
  simulation.

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
- **F** — switch to the Via tool
- **Space** — switch to Select mode
- **Q** — switch to the Input tool
- **E** — switch to the Output tool
- **R** — while an IC is pending placement (Components menu, Ctrl+C, or
  Ctrl+V), rotate it 90° counterclockwise before dropping it
- **1-9** — pick which layer new wires route on
- **Shift (hold)** — preview all layers at once; **Ctrl+Shift** toggles that
  preview locked on/off
- **Delete / Backspace** — delete the current selection
- **Escape** — cancel the current action, close an open popup, or drop the
  selection
- **Ctrl+C** — copy whatever's under the cursor (a component, a Section, or
  a Text Label) and, for a component, start placing copies at the cursor
- **Ctrl+V** — paste another copy of whatever was last copied; a Section or
  Text Label drops immediately at the cursor's current position
- **Ctrl+S** — save (falls through to Save As if the schematic has no path yet)
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
- **Section-Labeling / Text Label** — a separate group to the right of
  Components, for framing and annotating a schematic rather than building
  it; see "Annotations" below
- **Manage Data** — appears when a single AT28C64B is selected; opens a panel
  to view/edit its memory contents directly

### Annotations

**Section-Labeling** drags out a light-gray rectangle to frame off part of a
schematic, labeled at its top-right corner. In Select mode you can drag its
body to move it, drag any of its 4 corners to resize it, or double-click its
label to rename it — the small lock icon next to the label freezes all three
against accidental changes (a locked section can still be selected and
inspected, just not moved/resized/renamed, and stays out of Delete's way
too). Typing the label happens immediately after you draw the rectangle;
Enter or clicking elsewhere confirms it, Escape (or confirming empty)
discards the whole section.

**Text Label** places a single line of freestanding text with a click, and
starts you typing it immediately the same way. Double-click any placed label
to retype it. Neither tool draws anything electrical — both are purely for
keeping your bearings in a large schematic.

### Layers panel

Docked below the taskbar (or centered on the left edge, if set in Settings):
lists every layer with its color, name, and position in the stack. Click a
row to make it the active layer for new wires, double-click a name to rename
it, use the reorder arrows or delete button, or add a new layer from the row
at the bottom.
