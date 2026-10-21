#!/usr/bin/env python3
"""Generate the six panels of the enclosure at true size, as a 1:1 cutting layout.

This is the *shape* file, not yet the production cutting file: finger joints and kerf
compensation are deliberately left out until the cutter's kerf figure and joint preference are
known (they change every edge). What it does give you is an exact, printable set of outlines
with every hole in place, so the design can be checked with cardboard or paper before plywood
is committed.

    python3 panels.py            # writes panels-1to1.svg (millimetres, print at 100%)

Geometry notes
--------------
* Front panels are VERTICAL planes rotated in plan, so each one's true shape is a trapezoid:
  bottom width is the plan length of the V (not W/2), and the top edge rises from H at the
  outer end to H+PEAK at the ridge.
* Roof panels are true trapezoids: the front edge recedes by V as it runs out to the eave,
  because the front it meets is angled.
* Laser cutting is a through-cut, so the "mechanical wells" that hold the spare sticks are
  through-slots plus a glued floor strip underneath each row (FLOOR_STRIP parts below).
"""
import math
from pathlib import Path

P = {
    "W": 300.0, "H": 120.0, "PEAK": 55.0, "D": 110.0, "V": 22.0, "T": 4.0,
    "GRILL_D": 62.0, "GRILL_HOLE": 3.0,
    "BTN_COUNT": 4, "BTN_D": 13.0, "BTN_X": 52.0, "KNOB_HOLE": 8.0, "KNOB_X": 248.0,
    "CTRL_Y": 42.0,
    "SLOTS_PER_SIDE": 4, "SLOT_PITCH": 34.0, "SLOT_W": 14.0, "SLOT_H": 6.0,
    "FLOOR_DROP": 11.0,     # how far below the roof the slot floor sits
}
SHEET = (594.0, 841.0)      # A1 portrait
MARGIN = 15.0


def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


class Part:
    """A closed outline plus holes, in the part's own coordinates."""

    def __init__(self, name, outline, holes=None, notes=""):
        self.name, self.outline, self.holes, self.notes = name, outline, holes or [], notes
        xs = [p[0] for p in outline]
        ys = [p[1] for p in outline]
        self.w, self.h = max(xs) - min(xs), max(ys) - min(ys)


def grill_holes(cx, cy, r, spacing=11.0, hole=1.5):
    """Hex-ish grid of speaker holes, clipped to the circle, never on the seam line."""
    holes = []
    rows = int(2 * r / (spacing * 0.87)) + 1
    for iy in range(rows + 1):
        y = cy - r + iy * spacing * 0.87
        offset = (spacing / 2) if iy % 2 else 0.0
        cols = int(2 * r / spacing) + 2
        for ix in range(cols + 1):
            x = cx - r + offset + ix * spacing
            if math.hypot(x - cx, y - cy) > r - hole - 2:
                continue
            if abs(x - cx) < hole + 0.8:      # keep clear of the seam
                continue
            holes.append(("circle", x - cx, y - cy, hole))
    return holes


def grill_positions(cx, cy, r, spacing=11.0, hole=1.5):
    """Hex-packed speaker holes inside a circle, as absolute panel coordinates."""
    out = []
    rows = int(2 * r / (spacing * 0.87)) + 1
    for iy in range(rows + 1):
        y = cy - r + iy * spacing * 0.87
        offset = (spacing / 2) if iy % 2 else 0.0
        cols = int(2 * r / spacing) + 2
        for ix in range(cols + 1):
            x = cx - r + offset + ix * spacing
            if math.hypot(x - cx, y - cy) <= r - hole - 2:
                out.append((x, y, hole))
    return out


def front_panel(p, mirror):
    """Trapezoid: outer end height H, ridge height H+PEAK, width = plan length of the V.

    The grill straddles the seam, so each panel carries the half nearest its ridge edge; the
    buttons sit on the left panel and the knob on the right, each measured from its outer end
    so the two halves stay mirror images.
    """
    half, v = p["W"] / 2, p["V"]
    wp = math.hypot(half, v)                       # true width of one front panel
    outline = [(0, 0), (wp, 0), (wp, p["H"] + p["PEAK"]), (0, p["H"])]
    holes = []

    # speaker grill: centred on the ridge edge, half of it on this panel
    r = p["GRILL_D"] / 2
    for x, y, hr in grill_positions(wp, p["CTRL_Y"], r):
        if mirror:
            if x > 1.8:                            # right panel: keep the half inside
                holes.append(("circle", x, y, hr))
        else:
            if x < wp - 1.8:
                holes.append(("circle", x, y, hr))

    if mirror:
        holes.append(("circle", p["W"] - p["KNOB_X"], p["CTRL_Y"], p["KNOB_HOLE"] / 2))
    else:
        for i in range(p["BTN_COUNT"]):
            cx = p["BTN_X"] + (i - (p["BTN_COUNT"] - 1) / 2) * (p["BTN_D"] + 7)
            holes.append(("circle", cx, p["CTRL_Y"], p["BTN_D"] / 2))

    if mirror:
        # the right panel is the left one flipped, so its ridge edge is on its left
        outline = [(wp - x, y) for x, y in outline]
        holes = [("circle", wp - h[1], h[2], h[3]) for h in holes]

    return Part(f"front-{'R' if mirror else 'L'}", outline, holes,
                notes=f"{wp:.1f} wide, H {p['H']:.0f} to {p['H'] + p['PEAK']:.0f}")


def roof_panel(p, mirror):
    """Trapezoid in slope coordinates: front edge recedes by V at the eave."""
    half = p["W"] / 2
    L = math.hypot(half, p["PEAK"])                # slope length, ridge to eave
    D, V = p["D"], p["V"]
    outline = [(0, 0), (L, V), (L, D + V), (0, D + V)]
    holes = []
    # stick slots along the slope, centred in the panel's depth
    span = D + V
    for i in range(p["SLOTS_PER_SIDE"]):
        u = p["SLOT_W"] + 14 + i * p["SLOT_PITCH"]
        if u > L - p["SLOT_W"]:
            continue
        holes.append(("slot", u, span / 2, p["SLOT_W"], p["SLOT_H"]))
    if mirror:
        outline = [(L - x, y) for x, y in outline]
        holes = [("slot", L - h[1], h[2], h[3], h[4]) for h in holes]

    part = Part(f"roof-{'R' if mirror else 'L'}", outline, holes,
                notes=f"slope {L:.1f} long, {sum(1 for h in holes)} stick slots")
    part.slope_len = L
    return part


def base_panel(p):
    W, D, V = p["W"], p["D"], p["V"]
    outline = [(0, 0), (W / 2, -V), (W, 0), (W, D), (0, D)]
    holes = []
    # cable exit for the USB power lead, and a vent field so the amp can breathe
    holes.append(("slot", W / 2, D - 14, 34.0, 12.0))
    for row in range(3):
        for col in range(12):
            holes.append(("circle", 40 + col * 20, 30 + row * 18, 3.0))
    return Part("base", outline, holes, notes="V-shaped front edge matches the angled face")


def back_panel(p):
    W, H, PEAK = p["W"], p["H"], p["PEAK"]
    outline = [(0, 0), (W, 0), (W, H), (W / 2, H + PEAK), (0, H)]
    holes = [("circle", 12, 12, 3.2), ("circle", W - 12, 12, 3.2),
             ("circle", 12, H - 12, 3.2), ("circle", W - 12, H - 12, 3.2)]
    return Part("back (removable)", outline, holes, notes="4 x M3 screws into the side panels")


def side_panel(p, name):
    return Part(name, [(0, 0), (p["D"], 0), (p["D"], p["H"]), (0, p["H"])],
                notes=f"{p['D']:.0f} x {p['H']:.0f}")


def floor_strip(p, name):
    """Glued under a row of slots: laser cuts through, so the well needs a floor."""
    half = p["W"] / 2
    L = math.hypot(half, p["PEAK"])
    return Part(name, [(0, 0), (L * 0.72, 0), (L * 0.72, 26), (0, 26)],
                notes=f"glue {p['FLOOR_DROP']:.0f} mm under the roof, behind the slot row")


def pack(parts, sheet_w, sheet_h, margin):
    """Shelf packing: sort by height, fill rows left to right."""
    placed, x, y, row_h = [], margin, margin, 0.0
    for part in sorted(parts, key=lambda q: -q.h):
        if x + part.w + margin > sheet_w:
            x, y, row_h = margin, y + row_h + margin + 8, 0.0
        if y + part.h + margin > sheet_h:
            raise SystemExit("does not fit on one sheet")
        placed.append((part, x, y))
        x += part.w + margin
        row_h = max(row_h, part.h)
    return placed


def main():
    p = P
    parts = [
        front_panel(p, False), front_panel(p, True),
        roof_panel(p, False), roof_panel(p, True),
        base_panel(p), back_panel(p),
        side_panel(p, "side-L"), side_panel(p, "side-R"),
        floor_strip(p, "slot floor-L"), floor_strip(p, "slot floor-R"),
    ]

    out = []
    for part, ox, oy in pack(parts, *SHEET, MARGIN):
        xs = [q[0] for q in part.outline]
        ys = [q[1] for q in part.outline]
        minx, maxy = min(xs), max(ys)

        def sx(px):
            return ox + (px - minx)

        def sy(py):
            return oy + (maxy - py)

        pts = " ".join(f"{f(sx(x))},{f(sy(y))}" for x, y in part.outline)
        out.append(f'<polygon points="{pts}" fill="none" stroke="#000" stroke-width="0.3"/>')
        for hole in part.holes:
            if hole[0] == "circle":
                out.append(f'<circle cx="{f(sx(hole[1]))}" cy="{f(sy(hole[2]))}" '
                           f'r="{f(hole[3])}" fill="none" stroke="#000" stroke-width="0.3"/>')
            else:
                _, hx, hy, hw, hh = hole
                out.append(f'<rect x="{f(sx(hx - hw / 2))}" y="{f(sy(hy + hh / 2))}" '
                           f'width="{f(hw)}" height="{f(hh)}" fill="none" '
                           f'stroke="#000" stroke-width="0.3"/>')
        out.append(f'<text x="{f(ox)}" y="{f(oy + 5)}" font-family="DejaVu Sans,sans-serif" '
                   f'font-size="4" fill="#333">{part.name}</text>')
        if part.notes:
            out.append(f'<text x="{f(ox)}" y="{f(oy + 10)}" '
                       f'font-family="DejaVu Sans,sans-serif" font-size="3" fill="#777">'
                       f'{part.notes}</text>')

    # 1:1 verification aids
    out.append(f'<line x1="{f(MARGIN)}" y1="{f(SHEET[1] - 22)}" x2="{f(MARGIN + 100)}" '
               f'y2="{f(SHEET[1] - 22)}" stroke="#000" stroke-width="0.3"/>')
    out.append(f'<text x="{f(MARGIN)}" y="{f(SHEET[1] - 25)}" font-family="DejaVu Sans,sans-serif" '
               f'font-size="4" fill="#000">print at 100% - this line must measure 100.0 mm</text>')
    # thickness gauge: slots at nominal and +/-0.2 mm, so the sheet can be test-fitted
    for i, t in enumerate((p["T"] - 0.2, p["T"], p["T"] + 0.2)):
        x = MARGIN + 120 + i * 14
        out.append(f'<rect x="{f(x)}" y="{f(SHEET[1] - 30)}" width="{f(t)}" height="14" '
                   f'fill="none" stroke="#000" stroke-width="0.3"/>')
        out.append(f'<text x="{f(x)}" y="{f(SHEET[1] - 32)}" '
                   f'font-family="DejaVu Sans,sans-serif" font-size="3" fill="#333">'
                   f'{t:.1f}</text>')

    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{f(SHEET[0])}mm" '
           f'height="{f(SHEET[1])}mm" viewBox="0 0 {f(SHEET[0])} {f(SHEET[1])}">\n'
           '<rect width="100%" height="100%" fill="#fff"/>\n' + "\n".join(out) + "\n</svg>\n")
    path = Path(__file__).with_name("panels-1to1.svg")
    path.write_text(svg, encoding="utf-8")
    print(f"wrote {path}  ({SHEET[0]:.0f} x {SHEET[1]:.0f} mm, {len(parts)} parts)")


if __name__ == "__main__":
    main()
