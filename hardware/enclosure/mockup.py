#!/usr/bin/env python3
"""Generate a dimensioned three-view mockup of the music box enclosure.

Everything comes from PARAMS, so changing a size or the ply thickness is a one-line edit and
a re-run:

    python3 mockup.py            # writes mockup.svg beside this file

Units are millimetres, so the SVG prints 1:1 or can be measured directly. The cutting file is
a separate step: finger joints and kerf compensation only make sense once thickness and joint
scheme are settled.
"""
import math
from pathlib import Path

PARAMS = {
    # --- overall envelope ---------------------------------------------------------------
    "W": 300.0,          # overall width
    "H": 120.0,          # height at the ends (low edges of the roof)
    "PEAK": 55.0,        # how far the ridge rises above the ends
    "D": 110.0,          # depth, front face to back face
    "V_DEPTH": 22.0,     # how far the front ridge protrudes past the ends in plan
    # --- material -----------------------------------------------------------------------
    "T": 4.0,            # plywood thickness
    # --- front face furniture -----------------------------------------------------------
    "GRILL_D": 62.0,     # speaker grill diameter
    "BTN_COUNT": 4,      # media buttons
    "BTN_D": 13.0,       # button hole diameter
    "BTN_X": 52.0,       # centre of the button group
    "KNOB_D": 22.0,      # volume knob
    "KNOB_X": 248.0,     # knob centre
    "CTRL_Y": 42.0,      # centre line for buttons and knob
    # --- top ----------------------------------------------------------------------------
    "TEETH_PER_SIDE": 4, # comb teeth per roof slope
    "TOOTH_H": 38.0,     # tooth height above the slope
    "TOOTH_W": 9.0,
    "STICK_W": 22.0,     # USB stick width: sets the groove between teeth
    "USB_SLOT_W": 15.0,  # playing-stick opening at the ridge
}

MARGIN = 24.0
GAP = 34.0           # gap between views


def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


class Sheet:
    def __init__(self):
        self.parts = []
        self.width = 0.0
        self.height = 0.0

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

    def leader(self, x1, y1, x2, y2, s, size=3.4, anchor="middle"):
        """Callout: short line plus a label."""
        self.line(x1, y1, x2, y2, w=0.3, dash="1.5,1.5", colour="#555")
        self.text(x2, y2 - 1.5 if y2 < y1 else y2 + 4.5, s, size=size, anchor=anchor,
                  colour="#222")

    def dim(self, x1, y1, x2, y2, label, offset=0.0, size=3.4):
        dx, dy = x2 - x1, y2 - y1
        ln = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / ln, dx / ln
        ax1, ay1, ax2, ay2 = x1 + nx * offset, y1 + ny * offset, x2 + nx * offset, y2 + ny * offset
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
    """Height of the roof at horizontal position x (ridge at W/2)."""
    return p["H"] + p["PEAK"] - abs(x - p["W"] / 2) * (p["PEAK"] / (p["W"] / 2))


def front(s, ox, base, p):
    """Front elevation: the angled face, its furniture, and the roof with comb teeth."""
    W, H, PEAK, x0 = p["W"], p["H"], p["PEAK"], ox
    s.poly([(x0, base), (x0, base - H), (x0 + W / 2, base - H - PEAK),
            (x0 + W, base - H), (x0 + W, base)])

    # the two front panels meet at the ridge: a vertical seam down the middle
    s.line(x0 + W / 2, base, x0 + W / 2, base - H - PEAK, w=0.5, dash="2.5,2")

    # speaker grill straddling the ridge
    gx, gy = x0 + W / 2, base - p["CTRL_Y"]
    r = p["GRILL_D"] / 2
    s.circle(gx, gy, r, w=0.7)
    s.circle(gx, gy, r - 2.5, w=0.3, dash="1,1.5")
    for ring_r, count in ((r - 7, 16), (r - 16, 10), (r - 25, 5)):
        for i in range(count):
            a = 2 * math.pi * i / count + 0.3
            s.circle(gx + ring_r * math.cos(a), gy + ring_r * math.sin(a), 1.5, w=0.3)

    # media buttons
    bx, by = x0 + p["BTN_X"], base - p["CTRL_Y"]
    for i in range(p["BTN_COUNT"]):
        cx = bx + (i - (p["BTN_COUNT"] - 1) / 2) * (p["BTN_D"] + 7)
        s.circle(cx, by, p["BTN_D"] / 2, w=0.6)
        s.circle(cx, by, p["BTN_D"] / 2 - 2, w=0.25)

    # volume knob
    kx = x0 + p["KNOB_X"]
    s.circle(kx, by, p["KNOB_D"] / 2, w=0.9)
    s.circle(kx, by, p["KNOB_D"] / 2 - 3, w=0.3, dash="1.5,1")
    s.line(kx, by, kx, by - p["KNOB_D"] / 2 + 3, w=0.5)      # pointer

    # comb teeth standing on the roof slopes, with a spare stick resting between two
    for side in (-1, 1):
        first = W / 2 + side * (p["USB_SLOT_W"] / 2 + 13)
        step = p["STICK_W"] + p["TOOTH_W"] + 2
        for i in range(p["TEETH_PER_SIDE"]):
            x = first + side * i * step
            if not (8 < x < W - 8):
                continue
            hb = roof_h(x, p)
            s.poly([(x0 + x - p["TOOTH_W"] / 2, base - hb + 1),
                    (x0 + x - p["TOOTH_W"] / 2, base - hb - p["TOOTH_H"]),
                    (x0 + x + p["TOOTH_W"] / 2, base - hb - p["TOOTH_H"]),
                    (x0 + x + p["TOOTH_W"] / 2, base - hb + 1)], w=0.7)
        # a stored stick, lying in the groove between the first two teeth
        gx2 = first + side * step * 0.5
        if 12 < gx2 < W - 12:
            hb = roof_h(gx2, p)
            s.poly([(x0 + gx2 - p["STICK_W"] / 2 + 2, base - hb - 4),
                    (x0 + gx2 + p["STICK_W"] / 2 - 2, base - hb - p["TOOTH_H"] + 6),
                    (x0 + gx2 + p["STICK_W"] / 2 - 2, base - hb - p["TOOTH_H"] + 12),
                    (x0 + gx2 - p["STICK_W"] / 2 + 2, base - hb + 2)],
                   w=0.6, colour="#777", dash="2,1.5")

    # the playing stick, upright at the ridge, with its slot in the roof
    peak_y = base - H - PEAK
    s.poly([(x0 + W / 2 - p["USB_SLOT_W"] / 2, peak_y + 6),
            (x0 + W / 2 - p["USB_SLOT_W"] / 2, peak_y - 2),
            (x0 + W / 2 + p["USB_SLOT_W"] / 2, peak_y - 2),
            (x0 + W / 2 + p["USB_SLOT_W"] / 2, peak_y + 6)], w=0.6, colour="#777")
    s.poly([(x0 + W / 2 - 10, peak_y - 2), (x0 + W / 2 - 10, peak_y - 48),
            (x0 + W / 2 + 10, peak_y - 48), (x0 + W / 2 + 10, peak_y - 2)], w=0.9)
    s.circle(x0 + W / 2, peak_y - 36, 2.4, w=0.5)

    # callouts
    s.leader(x0 + W / 2 + r * 0.7, gy - r * 0.7, x0 + W / 2 + 46, gy - 46, "speaker grill")
    s.leader(x0 + p["BTN_X"], by - p["BTN_D"], x0 + p["BTN_X"] - 4, by - 26, "media buttons")
    s.leader(kx, by - p["KNOB_D"] / 2, kx + 6, by - 28, "volume knob")
    s.leader(x0 + W * 0.22, base - roof_h(W * 0.22, p) - p["TOOTH_H"] * 0.6,
             x0 + W * 0.12, base - H - PEAK - 18, "comb teeth: spare sticks rest between them")
    s.leader(x0 + W / 2 + 8, peak_y - 30, x0 + W / 2 + 40, peak_y - 56, "playing stick")

    # dimensions
    s.dim(x0, base + 12, x0 + W, base + 12, f"W {p['W']:.0f}")
    s.dim(x0 - 11, base, x0 - 11, base - H, f"H {p['H']:.0f}")
    s.dim(x0 + W + 11, base - H, x0 + W + 11, base - H - PEAK, f"{p['PEAK']:.0f}")
    s.dim(x0, base, x0 + p["BTN_X"], base, f"{p['BTN_X']:.0f}", offset=22)
    s.dim(x0, base, x0 + p["KNOB_X"], base, f"{p['KNOB_X']:.0f}", offset=29)
    s.text(x0 + W / 2, base + 26, "FRONT ELEVATION", size=5, bold=True)
    s.ensure(x0 + W + 34, base + 34)


def plan(s, ox, oy, p):
    """Plan: the shallow V front, the flat back, and the ridge carrying the teeth."""
    W, D, V = p["W"], p["D"], p["V_DEPTH"]
    s.poly([(ox, oy), (ox + W / 2, oy - V), (ox + W, oy), (ox + W, oy + D), (ox, oy + D)])
    s.line(ox + W / 2, oy - V, ox + W / 2, oy + D, w=0.5, dash="2.5,2")

    for side in (-1, 1):
        for i in range(p["TEETH_PER_SIDE"]):
            x = W / 2 + side * (p["USB_SLOT_W"] / 2 + 13 + i * (p["STICK_W"] + p["TOOTH_W"] + 2))
            if 8 < x < W - 8:
                s.line(ox + x, oy + 10, ox + x, oy + D - 10, w=0.3, dash="1.5,2.5")

    s.dim(ox, oy + D + 12, ox + W, oy + D + 12, f"W {W:.0f}")
    s.dim(ox + W + 11, oy - V, ox + W + 11, oy + D, f"D {D:.0f}")
    s.dim(ox + W / 2, oy, ox + W / 2, oy - V, f"V {V:.0f}", offset=6, size=3.0)
    s.text(ox + W / 2, oy + D + 25,
           "PLAN — front edge (top) is the shallow V; ridge runs front to back", size=4.6, bold=True)
    s.ensure(ox + W + 30, oy + D + 34)


def side(s, ox, base, p):
    """Side (right): depth against end height, ridge and stick shown behind."""
    H, D, PEAK = p["H"], p["D"], p["PEAK"]
    s.poly([(ox, base), (ox + D, base), (ox + D, base - H), (ox, base - H)])
    s.line(ox, base - H - PEAK, ox + D, base - H - PEAK, w=0.45, dash="2.5,2")
    s.text(ox + D / 2, base - H - PEAK - 5, "ridge (hidden behind the side wall)",
           size=3.0, colour="#555")
    sx = ox + D / 2 - 10
    s.poly([(sx, base - H - PEAK), (sx, base - H - PEAK - 46),
            (sx + 20, base - H - PEAK - 46), (sx + 20, base - H - PEAK)], w=0.8, colour="#777",
           dash="2,1.5")
    s.dim(ox, base + 12, ox + D, base + 12, f"D {D:.0f}")
    s.dim(ox + D + 11, base, ox + D + 11, base - H - PEAK, f"{H + PEAK:.0f}")
    s.text(ox + D / 2, base + 24, "SIDE (right) — stick shown dashed", size=4.6, bold=True)
    s.ensure(ox + D + 30, base + 34)


def main():
    p = PARAMS
    s = Sheet()
    header = 22
    base = header + p["H"] + p["PEAK"] + 58

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
