#!/usr/bin/env python3
"""True-size panel shapes for the enclosure, with every hole.

Shapes only: finger joints and kerf compensation come after the fit coupon settles the kerf.
Print at 100% and check with cardboard first.

    python3 panels.py            # writes panels-1to1.svg

Derived geometry, all from PARAMS
--------------------------------
run      = (W - FLAT)/2                       horizontal run of one roof slope
peak     = run * tan(ROOF_DEG)                 rise from the end height to the plateau
slope    = run / cos(ROOF_DEG)                 length of the roof plate, plateau edge to eave
D        = overall depth, back face to the front ridge apex
side_dep = D - V                               depth of the flat side walls

A front panel is a vertical plane rotated in plan, so its true width is the plan length
hypot(W/2, V) and its top edge climbs from H to H+peak over the run, then runs flat under the
plateau. A roof plate is a trapezoid: 90 long at the plateau and (90 - V) at the eave, because
the front it meets is angled.
"""
import math
from pathlib import Path

P = {
    "W": 200.0, "H": 50.0, "ROOF_DEG": 40.0, "FLAT": 36.0,
    "D": 90.0, "V": 16.0, "T": 4.0,
    "GRILL_D": 44.0, "GRILL_HOLE": 1.5,
    "BTN_COUNT": 4, "BTN_D": 12.0, "BTN_X": 38.0,
    "KNOB_HOLE": 8.0, "KNOB_X": 168.0, "CTRL_Y": 26.0,
    "SLOTS_PER_SIDE": 3, "SLOT_W": 11.0, "SLOT_H": 5.0,
    "USB_W": 15.0, "USB_H": 7.0,
    "FLOOR_DROP": 11.0,
}
SHEET = (420.0, 594.0)
MARGIN = 14.0


def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


def derived(p):
    run = (p["W"] - p["FLAT"]) / 2
    peak = run * math.tan(math.radians(p["ROOF_DEG"]))
    slope = run / math.cos(math.radians(p["ROOF_DEG"]))
    return {"run": run, "peak": peak, "slope": slope,
            "side_depth": p["D"] - p["V"], "halfwidth": p["W"] / 2}


class Part:
    def __init__(self, name, outline, holes=None, notes=""):
        self.name, self.outline, self.holes, self.notes = name, outline, holes or [], notes


def grill_positions(cx, cy, r, spacing=9.0, hole=1.5):
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


def front_panel(p, d, mirror):
    """Pentagon: base, ridge edge, flat under the plateau, and the slope down to the end."""
    half, v = p["W"] / 2, p["V"]
    wp = math.hypot(half, v)                       # true width of one front panel
    plateau_s = ((half - p["FLAT"] / 2) / half) * wp  # where the roof levels off
    top = p["H"] + d["peak"]
    outline = [(0, 0), (wp, 0), (wp, top), (plateau_s, top), (0, p["H"])]

    holes = []
    r = p["GRILL_D"] / 2
    for x, y, hr in grill_positions(wp, p["CTRL_Y"], r):
        if mirror:
            if x > 1.8:
                holes.append(("circle", x, y, hr))
        else:
            if x < wp - 1.8:
                holes.append(("circle", x, y, hr))
    if mirror:
        holes.append(("circle", p["W"] - p["KNOB_X"], p["CTRL_Y"], p["KNOB_HOLE"] / 2))
    else:
        for i in range(p["BTN_COUNT"]):
            holes.append(("circle",
                          p["BTN_X"] + (i - (p["BTN_COUNT"] - 1) / 2) * (p["BTN_D"] + 6),
                          p["CTRL_Y"], p["BTN_D"] / 2))
    if mirror:
        outline = [(wp - x, y) for x, y in outline]
        holes = [("circle", wp - h[1], h[2], h[3]) for h in holes]
    return Part(f"front-{'R' if mirror else 'L'}", outline, holes,
                f"{wp:.1f} wide, {p['H']:.0f} to {top:.1f} high")


def roof_panel(p, d, mirror):
    """Trapezoid: 90 at the plateau, 74 at the eave, slope long, slots along the middle."""
    L, D, V = d["slope"], p["D"], p["V"]
    outline = [(0, 0), (L, V), (L, D), (0, D)]
    holes = []
    for i in range(p["SLOTS_PER_SIDE"]):
        u = L / (p["SLOTS_PER_SIDE"] + 1) * (i + 1)
        holes.append(("slot", u, D / 2, p["SLOT_W"], p["SLOT_H"]))
    if mirror:
        outline = [(L - x, y) for x, y in outline]
        holes = [("slot", L - h[1], h[2], h[3], h[4]) for h in holes]
    return Part(f"roof-{'R' if mirror else 'L'}", outline, holes,
                f"slope {L:.0f}, {D:.0f} at the plateau, {D - V:.0f} at the eave")


def plateau_plate(p):
    return Part("plateau", [(0, 0), (p["FLAT"], 0), (p["FLAT"], p["D"]), (0, p["D"])],
                [("slot", p["FLAT"] / 2, p["D"] / 2, p["USB_W"], p["USB_H"])],
                f"{p['FLAT']:.0f} x {p['D']:.0f} - carries the playing stick's socket")


def side_panel(p, d, name):
    return Part(name, [(0, 0), (d["side_depth"], 0), (d["side_depth"], p["H"]), (0, p["H"])],
                notes=f"{d['side_depth']:.0f} x {p['H']:.0f}")


def base_panel(p, d):
    W, D, V, sd = p["W"], p["D"], p["V"], d["side_depth"]
    outline = [(0, 0), (W, 0), (W, sd), (W / 2, D), (0, sd)]
    holes = [("slot", W / 2, D - 16, 30.0, 11.0)]           # power lead exit
    for row in range(2):
        for col in range(9):
            holes.append(("circle", 30 + col * 18, 22 + row * 16, 3.0))
    return Part("base", outline, holes, "V front edge matches the angled face")


def back_panel(p, d):
    W, H, peak = p["W"], p["H"], d["peak"]
    outline = [(0, 0), (W, 0), (W, H), (W / 2, H + peak), (0, H)]
    holes = [("circle", 10, 10, 3.2), ("circle", W - 10, 10, 3.2),
             ("circle", 10, H - 10, 3.2), ("circle", W - 10, H - 10, 3.2)]
    return Part("back (removable)", outline, holes, f"{W:.0f} wide, 4 x M3 screws")


def floor_strip(p, d, name):
    return Part(name, [(0, 0), (d["slope"] * 0.8, 0), (d["slope"] * 0.8, 24), (0, 24)],
                notes=f"glue {p['FLOOR_DROP']:.0f} mm below the roof, behind the slot row")


def pack(parts, sheet_w, sheet_h, margin):
    placed, x, y, row_h = [], margin, margin, 0.0
    for part in sorted(parts, key=lambda q: -(max(p[1] for p in q.outline)
                                              - min(p[1] for p in q.outline))):
        w = max(p[0] for p in part.outline) - min(p[0] for p in part.outline)
        h = max(p[1] for p in part.outline) - min(p[1] for p in part.outline)
        if x + w + margin > sheet_w:
            x, y, row_h = margin, y + row_h + margin + 10, 0.0
        placed.append((part, x, y))
        x += w + margin
        row_h = max(row_h, h)
    return placed


def main():
    p, d = P, derived(P)
    parts = [
        front_panel(p, d, False), front_panel(p, d, True),
        roof_panel(p, d, False), roof_panel(p, d, True),
        plateau_plate(p), base_panel(p, d), back_panel(p, d),
        side_panel(p, d, "side-L"), side_panel(p, d, "side-R"),
        floor_strip(p, d, "slot floor-L"), floor_strip(p, d, "slot floor-R"),
    ]

    out = []
    for part, ox, oy in pack(parts, *SHEET, MARGIN):
        xs = [q[0] for q in part.outline]
        ys = [q[1] for q in part.outline]
        minx, maxy = min(xs), max(ys)
        sx = lambda px: ox + (px - minx)
        sy = lambda py: oy + (maxy - py)
        out.append('<polygon points="' + " ".join(f"{f(sx(x))},{f(sy(y))}"
                                                  for x, y in part.outline)
                   + '" fill="none" stroke="#000" stroke-width="0.3"/>')
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
                   f'font-size="3.6" fill="#333">{part.name}</text>')
        if part.notes:
            out.append(f'<text x="{f(ox)}" y="{f(oy + 9.5)}" '
                       f'font-family="DejaVu Sans,sans-serif" font-size="2.8" fill="#777">'
                       f'{part.notes}</text>')

    out.append(f'<line x1="{f(MARGIN)}" y1="{f(SHEET[1] - 20)}" x2="{f(MARGIN + 100)}" '
               f'y2="{f(SHEET[1] - 20)}" stroke="#000" stroke-width="0.3"/>')
    out.append(f'<text x="{f(MARGIN)}" y="{f(SHEET[1] - 24)}" '
               f'font-family="DejaVu Sans,sans-serif" font-size="3.6" fill="#000">'
               f'print at 100% - this line must measure 100.0 mm</text>')
    for i, t in enumerate((p["T"] - 0.2, p["T"], p["T"] + 0.2)):
        x = MARGIN + 118 + i * 12
        out.append(f'<rect x="{f(x)}" y="{f(SHEET[1] - 28)}" width="{f(t)}" height="12" '
                   f'fill="none" stroke="#000" stroke-width="0.3"/>')

    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{f(SHEET[0])}mm" '
           f'height="{f(SHEET[1])}mm" viewBox="0 0 {f(SHEET[0])} {f(SHEET[1])}">\n'
           '<rect width="100%" height="100%" fill="#fff"/>\n' + "\n".join(out) + "\n</svg>\n")
    path = Path(__file__).with_name("panels-1to1.svg")
    path.write_text(svg, encoding="utf-8")
    print(f"wrote {path}  ({len(parts)} parts on {SHEET[0]:.0f} x {SHEET[1]:.0f} mm)")
    print(f"  run {d['run']:.1f}, peak {d['peak']:.1f}, slope {d['slope']:.1f}, "
          f"side depth {d['side_depth']:.1f}")


if __name__ == "__main__":
    main()
