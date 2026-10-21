# Enclosure: laser-cut plywood

A parametric three-view mockup of the box, generated from the 1:1 pencil sketch and the
component dimensions. `python3 mockup.py` rewrites `mockup.svg` (millimetres, prints 1:1).

## The reading of the sketch

- **Front elevation:** flat base, straight ends, and a **tent roof** — ridge at the centre,
  both slopes falling to the ends.
- 🔑 **The "teeth" are the USB sticks themselves.** They are not wood: each spare stick plugs
  into its own slot in the roof and stands **perpendicular to the slope**, which is why the
  sketch's spikes lean outward. The slot at the ridge is the wired port that plays; the rest
  are storage. (Correction from the first draft, which drew wooden teeth.)
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
| roof slots | 4 per side, ⌀14 × 11 deep, 34 pitch | a stick stands in each; the connector is ≈ 12.5 mm wide |
| stick clear height | 46 | leaves room to grab one |
| back panel | flat, removable | a plain rectangle between the side panels |

## To confirm before the cut file

1. **Plywood thickness** — 3, 4 or 6 mm. Every joint dimension follows from it.
2. **Speaker** — the actual driver diameter and depth; the grill and internal clearance depend
   on it.
3. **Are the spare slots mechanical or wired?** A blind 11 mm well simply holds a stick
   upright (cheap, and only the ridge slot needs a real USB socket wired to the board). Wiring
   all eight through a hub would be a much bigger job for no benefit — assume mechanical
   unless told otherwise.
4. **Slot count** — 4 per side as sketched (8 spare + 1 playing), or should it hold more?
5. **Size** — 300 × 175 × 110 mm overall; fine for a shelf, but say if it should shrink.

## What the cut file will contain

Once those are settled, `panels.py` will emit an SVG with every part laid out for cutting:

- two **front panels**, mirrored, each carrying **half the speaker grill** (the circle straddles
  the seam), the button holes and the interlocking seam;
- two **roof slopes** with the row of stick slots cut through them, plus a reinforced socket
  position at the ridge for the wired port;
- two **side panels**, and the **base** and **back**;
- **finger joints** everywhere (glued, no fasteners visible), with **kerf compensation**
  (typically 0.1–0.2 mm per side — the cutter's own figure wins);
- a **1:1 check strip** — a 100 mm line to measure the print, and a slot sized to the real
  plywood, so a mis-scaled job or a thick sheet is caught before cutting.
