# Enclosure: laser-cut plywood

A parametric three-view mockup of the box, generated from the 1:1 pencil sketch and the
component dimensions. `python3 mockup.py` rewrites `mockup.svg` (millimetres, prints 1:1).

## The reading of the sketch

- **Front elevation:** flat base, straight ends, and a **tent roof** — ridge at the centre,
  both slopes falling to the ends. The **comb teeth** stand on those slopes, so spare sticks
  rest upright in the grooves between them.
- **Plan:** the front face is a **shallow V**, so each front panel is angled toward the viewer
  and they meet at the ridge. That meeting line is the seam in the sketch, and it is where the
  two front pieces interlock.
- **Front furniture**, left to right: four **media buttons**, the **speaker grill** straddling
  the seam, and the **volume knob** to its right.
- **Playing stick:** plugs into a slot at the ridge, standing upright.

## Current dimensions (all mm)

| | value | where it comes from |
| --- | --- | --- |
| width `W` | 300 | proportion of the sketch, checked against the hardware layout |
| end height `H` | 120 | leaves room for driver + board + a felt-lined base |
| ridge rise `PEAK` | 55 | roof slope ≈ 20°, steep enough that sticks lean without falling |
| depth `D` | 110 | ESP32 board (≈55 mm) + speaker magnet + wiring service loop |
| front V `V_DEPTH` | 22 | "slightly angled": ≈ 8° per panel |
| plywood `T` | 4 | thin enough to cut cleanly, thick enough for finger joints |
| grill `GRILL_D` | 62 | matches a 50–55 mm driver plus a ring of 3 mm holes |
| buttons | 4 × ⌀13 | prev, play/pause, next, stop |
| knob | ⌀22 | KY-040 with a printed cap |
| teeth | 4 per side, 9 wide × 38 tall, 22 gaps | a USB stick is ≈ 20 mm wide |

## To confirm before the cut file

1. **Plywood thickness** — 3, 4 or 6 mm. Every joint dimension follows from it.
2. **Speaker** — the actual driver diameter and depth; the grill and internal clearance depend
   on it.
3. **Spare sticks upright** in the comb (as drawn) — or lying flat across the roof?
4. **How the box opens** — a screwed base plate (simplest, invisible from the front) or a
   removable back panel (better for servicing the board).
5. **Size** — 300 × 175 × 110 mm overall; fine for a shelf, but say if it should shrink.

## What the cut file will contain

Once those are settled, `panels.py` will emit an SVG with every part laid out for cutting:

- two **front panels**, mirrored, with the grill half, button holes and the interlocking seam;
- two **roof slopes** carrying the comb teeth, with slots for the ridge and the stick opening;
- two **side panels**, and the **base** and **back**;
- **finger joints** everywhere (glued, no fasteners visible), with **kerf compensation**
  (typically 0.1–0.2 mm per side — the cutter's own figure wins);
- a **1:1 check strip** — a 100 mm line to measure the print, and a slot sized to the real
  plywood, so a mis-scaled job or a thick sheet is caught before cutting.
