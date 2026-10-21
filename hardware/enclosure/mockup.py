#!/usr/bin/env python3
"""Dimensioned three-view mockup of the music box enclosure.

The "comb" on the roof is not cut into the wood: the teeth in the sketch are the USB sticks
themselves, each plugged into its own slot in the roof and standing perpendicular to the
slope. One slot at the ridge is the wired port that plays; the rest are storage.

Everything comes from PARAMS:

    python3 mockup.py            # writes mockup.svg beside this file

Millimetres throughout, so the SVG prints 1:1.
"""
import math
from pathlib import Path

PARAMS = {
    # --- envelope -----------------------------------------------------------------------
    "W": 300.0,          # overall width
    "H": 120.0,          # height at the ends
    "PEAK": 55.0,        # ridge rise above the ends
    "D": 110.0,          # depth: flat front-to-back
    "V_DEPTH": 22.0,     # how far the front ridge protrudes past the ends (the angled front)
    # --- material -----------------------------------------------------------------------
    "T": 4.0,            # plywood thickness
    # --- front face ---------------------------------------------------------------------
    "GRILL_D": 62.0,     # speaker grill (fits a ~50 mm driver)
    "BTN_COUNT": 4,
    "BTN_D": 13.0,
    "BTN_X": 52.0,
    "KNOB_D": 22.0,
    "KNOB_X": 248.0,
    "CTRL_Y": 42.0,
    # --- roof: one slot per stored stick ------------------------------------------------
    "SLOTS_PER_SIDE": 4,  # spare sticks each side of the ridge
    "SLOT_PITCH": 34.0,   # spacing between slot centres along the slope
    "SLOT_W": 14.0,       # opening for the stick's metal connector (USB-A is ~12.5 mm)
    "SLOT_D": 11.0,       # how deep the connector sits below the surface
    "STICK_LEN": 46.0,    # how far a stick stands proud of the roof
    "STICK_W": 20.0,      # stick width
}

MARGIN = 24.0
GAP = 34.0


def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


class Sheet:
    def __init__(self):
        self.parts, self.width, self.height = [], 0.0, 0.0

    def ensure(self, w, h):
        self.width, self.height = max(self.width, w), max(self.height, h)

    def line(self, x1, y1, x2, y2, w=0.6, dash=None, colour="#111"):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<line x1="{f(x1)}" y1="{f(y1)}" x2="{f(x2)}" y2="{f(y2)}" '
                          f'stroke="{colour}" stroke-width="{w}"{d}/>')

    def poly(self, pts, w=0.9, closed=True, colour="#111", fill="none", dash=None):
        s = " ".join(f"{f(x)},{f(y)}" for x, y in pts)
        tag = "polygon" if closed else "polyline"
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<{tag} points="{s}" fill="{fill}" stroke="{colour}" '
                          f'stroke-width="{w}"{d}/>')

    def circle(self, cx, cy, r, w=0.8, dash=None, fill="none", colour="#111"):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<circle cx="{f(cx)}" cy="{f(cy)}" r="{f(r)}" fill="{fill}" '
                          f'stroke="{colour}" stroke-width="{w}"{d}/>')

    def text(self, x, y, s, size=4.0, anchor="middle", bold=False, colour="#111"):
        b = ' font-weight="bold"' if bold else ""
        self.parts.append(f'<text x="{f(x)}" y="{f(y)}" font-family="DejaVu Sans,sans-serif" '
                          f'font-size="{f(size)}" text-anchor="{anchor}" fill="{colour}"{b}>'
                          f'{s}</text>')

    def leader(self, x1, y1, x2, y2, s, size=3.4):
        self.line(x1, y1, x2, y2, w=0.3, dash="1.5,1.5", colour="#555")
        self.text(x2, y2 - 1.5 if y2 < y1 else y2 + 4.5, s, size=size, colour="#222")

    def dim(self, x1, y1, x2, y2, label, offset=0.0, size=3.4):
        dx, dy = x2 - x1, y2 - y1
        ln = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / ln, dx / ln
        ax1, ay1 = x1 + nx * offset, y1 + ny * offset
        ax2, ay2 = x2 + nx * offset, y2 + ny * offset
        self.line(ax1, ay1, ax2, ay2, w=0.3)
        for px, py in ((ax1, ay1), (ax2, ay2)):
            self.line(px - nx * 1.4, py - ny * 1.4, px + nx * 1.4, py + ny * 1.4, w=0.3)
        self.text((ax1 + ax2) / 2 + nx * 3.0, (ay1 + ay2) / 2 + ny * 3.0, label, size=size)

    def svg(self):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{f(self.width)}mm" '
                f'height="{f(self.height)}mm" viewBox="0 0 {f(self.width)} {f(self.height)}">\n'
                '<rect width="100%" height="100%" fill="#fff"/>\n'
                + "\n".join(self.parts) + "\n</svg>\n")


def roof_h(x, p):
    """Roof height at horizontal position x, ridge at W/2 (y up)."""
    return p["H"] + p["PEAK"] - abs(x - p["W"] / 2) * (p["PEAK"] / (p["W"] / 2))


def roof_normal(x, p):
    """Outward normal of the roof at x, as a unit vector in maths coordinates (y up)."""
    half = p["W"] / 2
    slope = p["PEAK"] / half                 # rise per unit run
    dx = 1.0 if x > p["W"] / 2 else -1.0
    # surface tangent (dx, dx*slope) -> normal (dx*slope, -1) normalised, flipped upward
    nx, ny = dx * slope, 1.0
    ln = math.hypot(nx, ny)
    return nx / ln, ny / ln


def stick_shape(slot_x, slot_y, normal, length, width):
    """Quad for a stick standing on a slot, in maths coords."""
    nx, ny = normal
    tx, ty = -ny, nx                          # along the slot, perpendicular to the stick
    tip = (slot_x + nx * length, slot_y + ny * length)
    return [(slot_x - tx * width / 2, slot_y - ty * width / 2),
            (slot_x + tx * width / 2, slot_y + ty * width / 2),
            (tip[0] + tx * width / 2, tip[1] + ty * width / 2),
            (tip[0] - tx * width / 2, tip[1] - ty * width / 2)]


def front(s, ox, base, p):
    """Front elevation: angled face, furniture, and the roof with its slots and sticks."""
    W, H, PEAK = p["W"], p["H"], p["PEAK"]

    def X(x):
        return ox + x

    def Y(y):
        return base - y

    s.poly([(X(0), Y(0)), (X(0), Y(H)), (X(W / 2), Y(H + PEAK)), (X(W), Y(H)), (X(W), Y(0))])
    s.line(X(W / 2), Y(0), X(W / 2), Y(H + PEAK), w=0.5, dash="2.5,2")

    # speaker grill, straddling the seam
    gx, gy, r = W / 2, p["CTRL_Y"], p["GRILL_D"] / 2
    s.circle(X(gx), Y(gy), r, w=0.7)
    s.circle(X(gx), Y(gy), r - 2.5, w=0.3, dash="1,1.5")
    for ring_r, count in ((r - 7, 16), (r - 16, 10), (r - 25, 5)):
        for i in range(count):
            a = 2 * math.pi * i / count + 0.3
            s.circle(X(gx + ring_r * math.cos(a)), Y(gy + ring_r * math.sin(a)), 1.5, w=0.3)

    # buttons
    for i in range(p["BTN_COUNT"]):
        cx = p["BTN_X"] + (i - (p["BTN_COUNT"] - 1) / 2) * (p["BTN_D"] + 7)
        s.circle(X(cx), Y(p["CTRL_Y"]), p["BTN_D"] / 2, w=0.6)
        s.circle(X(cx), Y(p["CTRL_Y"]), p["BTN_D"] / 2 - 2, w=0.25)

    # knob
    s.circle(X(p["KNOB_X"]), Y(p["CTRL_Y"]), p["KNOB_D"] / 2, w=0.9)
    s.circle(X(p["KNOB_X"]), Y(p["CTRL_Y"]), p["KNOB_D"] / 2 - 3, w=0.3, dash="1.5,1")
    s.line(X(p["KNOB_X"]), Y(p["CTRL_Y"]), X(p["KNOB_X"]), Y(p["CTRL_Y"] + p["KNOB_D"] / 2 - 3))

    # roof slots, with the stored sticks standing perpendicular to each slope
    for side in (-1, 1):
        for i in range(p["SLOTS_PER_SIDE"]):
            sx = W / 2 + side * (p["SLOT_W"] + 14 + i * p["SLOT_PITCH"])
            if not (10 < sx < W - 10):
                continue
            sy = roof_h(sx, p)
            n = roof_normal(sx, p)
            # the slot itself: a dark opening on the roof line
            s.line(X(sx - p["SLOT_W"] / 2), Y(sy), X(sx + p["SLOT_W"] / 2), Y(sy),
                   w=2.2, colour="#666")
            # the stick standing in it
            quad = stick_shape(sx, sy, n, p["STICK_LEN"], p["STICK_W"])
            s.poly([(X(qx), Y(qy)) for qx, qy in quad], w=0.8)
            # a hint of the connector below the surface
            s.poly([(X(sx - p["SLOT_W"] / 2), Y(sy)),
                    (X(sx - p["SLOT_W"] / 2), Y(sy - p["SLOT_D"])),
                    (X(sx + p["SLOT_W"] / 2), Y(sy - p["SLOT_D"])),
                    (X(sx + p["SLOT_W"] / 2), Y(sy))], w=0.35, colour="#888", dash="1.5,1.5")

    # the playing stick: vertical, at the ridge, in the wired port
    peak = H + PEAK
    s.line(X(W / 2 - p["SLOT_W"] / 2), Y(peak), X(W / 2 + p["SLOT_W"] / 2), Y(peak),
           w=2.2, colour="#000")
    s.poly([(X(W / 2 - 10), Y(peak)), (X(W / 2 - 10), Y(peak + 46)),
            (X(W / 2 + 10), Y(peak + 46)), (X(W / 2 + 10), Y(peak))], w=0.9)
    s.circle(X(W / 2), Y(peak + 36), 2.4, w=0.5)

    # callouts
    s.leader(X(W / 2 + r * 0.7), Y(gy + r * 0.7), X(W / 2 + 48), Y(gy + 46), "speaker grill")
    s.leader(X(p["BTN_X"]), Y(p["CTRL_Y"] + p["BTN_D"]), X(p["BTN_X"] - 4), Y(p["CTRL_Y"] + 26),
             "media buttons")
    s.leader(X(p["KNOB_X"]), Y(p["CTRL_Y"] + p["KNOB_D"] / 2), X(p["KNOB_X"] + 6),
             Y(p["CTRL_Y"] + 28), "volume knob")
    sx_probe = W / 2 + (p["SLOT_W"] + 14 + p["SLOT_PITCH"] * 1.5)
    s.leader(X(sx_probe), Y(roof_h(sx_probe, p) + p["STICK_LEN"] * 0.8),
             X(sx_probe) - 10, Y(H + PEAK + 24),
             "spare sticks stand in roof slots")
    s.leader(X(W / 2 + 9), Y(peak + 30), X(W / 2 + 42), Y(peak + 58), "playing stick (wired port)")

    s.dim(X(0), Y(0) + 12, X(W), Y(0) + 12, f"W {p['W']:.0f}")
    s.dim(X(0) - 11, Y(0), X(0) - 11, Y(H), f"H {p['H']:.0f}")
    s.dim(X(W) + 11, Y(H), X(W) + 11, Y(H + PEAK), f"{p['PEAK']:.0f}")
    s.dim(X(0), Y(0), X(p["BTN_X"]), Y(0), f"{p['BTN_X']:.0f}", offset=22)
    s.dim(X(0), Y(0), X(p["KNOB_X"]), Y(0), f"{p['KNOB_X']:.0f}", offset=29)
    s.text(X(W / 2), Y(0) + 26, "FRONT ELEVATION", size=5, bold=True)
    s.ensure(X(W) + 34, base + 34)


def plan(s, ox, oy, p):
    """Plan: shallow V front, flat back, ridge with the slot row running front to back."""
    W, D, V = p["W"], p["D"], p["V_DEPTH"]
    s.poly([(ox, oy), (ox + W / 2, oy - V), (ox + W, oy), (ox + W, oy + D), (ox, oy + D)])
    s.line(ox + W / 2, oy - V, ox + W / 2, oy + D, w=0.5, dash="2.5,2")

    # slots along the ridge: short openings, drawn as dark bars
    for side in (-1, 1):
        for i in range(p["SLOTS_PER_SIDE"]):
            sx = W / 2 + side * (p["SLOT_W"] + 14 + i * p["SLOT_PITCH"])
            if 10 < sx < W - 10:
                s.line(ox + sx, oy + 14, ox + sx, oy + D - 14, w=0.4, dash="1.5,2.5")
    s.line(ox + W / 2, oy - V + 14, ox + W / 2, oy + D - 14, w=1.6, colour="#000")

    s.dim(ox, oy + D + 12, ox + W, oy + D + 12, f"W {W:.0f}")
    s.dim(ox + W + 11, oy - V, ox + W + 11, oy + D, f"D {D:.0f}")
    s.dim(ox + W / 2, oy, ox + W / 2, oy - V, f"V {V:.0f}", offset=7, size=3.0)
    s.text(ox + W / 2, oy + D + 25,
           "PLAN — front edge (top) is the shallow V; ridge slots run front to back",
           size=4.6, bold=True)
    s.ensure(ox + W + 30, oy + D + 34)


def side(s, ox, base, p):
    """Side (right): depth against end height; flat removable back panel at the right."""
    H, D, PEAK = p["H"], p["D"], p["PEAK"]
    s.poly([(ox, base), (ox + D, base), (ox + D, base - H), (ox, base - H)])
    s.line(ox, base - H - PEAK, ox + D, base - H - PEAK, w=0.45, dash="2.5,2")
    s.text(ox + D / 2, base - H - PEAK - 4, "ridge (hidden behind the side wall)",
           size=3.0, colour="#555")
    # sticks standing perpendicular to the (invisible) roof, near the ridge line
    sx = ox + D * 0.45
    s.poly([(sx, base - H - PEAK), (sx, base - H - PEAK - 46),
            (sx + 14, base - H - PEAK - 46), (sx + 14, base - H - PEAK)], w=0.8,
           colour="#777", dash="2,1.5")
    # the flat back panel, removable
    s.line(ox + D, base, ox + D, base - H, w=1.6)
    s.text(ox + D + 6, base - H / 2, "back panel", size=3.2, anchor="start", colour="#333")
    s.text(ox + D + 6, base - H / 2 + 4, "(flat, removable)", size=3.0, anchor="start",
           colour="#666")
    s.dim(ox, base + 12, ox + D, base + 12, f"D {D:.0f}")
    s.dim(ox + D + 26, base, ox + D + 26, base - H - PEAK, f"{H + PEAK:.0f}")
    s.text(ox + D / 2, base + 24, "SIDE (right)", size=4.6, bold=True)
    s.ensure(ox + D + 44, base + 34)


def main():
    p = PARAMS
    s = Sheet()
    base = 22 + p["H"] + p["PEAK"] + 58

    front(s, MARGIN, base, p)
    side(s, MARGIN + p["W"] + GAP + 20, base, p)
    plan(s, MARGIN, base + 74, p)

    s.text(s.width / 2, 12,
           f"Music box enclosure — mockup · {p['T']:g} mm plywood · dimensions in mm",
           size=5.4, bold=True)

    out = Path(__file__).with_name("mockup.svg")
    out.write_text(s.svg(), encoding="utf-8")
    print(f"wrote {out}  ({s.width:.0f} x {s.height:.0f} mm)")


if __name__ == "__main__":
    main()
