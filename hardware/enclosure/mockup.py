#!/usr/bin/env python3
"""Dimensioned mockup of the music box enclosure.

Layout, in the order the user described it: flat base, short vertical ends, steep roof slopes at
ROOF_DEG rising to a FLAT PLATEAU, and the two front panels angled in plan (a shallow V) meeting
at a vertical ridge. Spare sticks stand in slots on the slopes turned SIDEWAYS, so from the front
only their thin edge shows; the playing stick sits in the plateau facing the viewer, label
readable.

    python3 mockup.py            # writes mockup.svg

Millimetres throughout; the SVG prints 1:1.
"""
import math
from pathlib import Path

P = {
    # --- envelope -----------------------------------------------------------------------
    "W": 200.0,          # overall width
    "H": 50.0,           # straight end height, before the roof starts
    "ROOF_DEG": 40.0,    # roof slope, degrees from horizontal
    "FLAT": 36.0,        # width of the flat plateau at the top
    "D": 90.0,           # overall depth, back face to the front ridge (the thickest point)
    "V": 16.0,           # how far the front ridge stands proud of the front's outer corners
    "T": 4.0,            # plywood thickness
    # --- front furniture ----------------------------------------------------------------
    "GRILL_D": 44.0,
    "BTN_COUNT": 4,
    "BTN_D": 12.0,
    "BTN_X": 38.0,
    "KNOB_D": 20.0,
    "KNOB_X": 168.0,
    "CTRL_Y": 26.0,
    # --- roof slots ---------------------------------------------------------------------
    "SLOTS_PER_SIDE": 3,
    "SLOT_W": 11.0,      # slot width seen from the front = the stick's THIN dimension
    "STICK_LEN": 46.0,
    "STICK_THIN": 10.0,  # stick thickness (what the front view shows for spare sticks)
    "STICK_WIDE": 20.0,  # stick width (what the front view shows for the playing stick)
}

MARGIN = 22.0
GAP = 30.0


def f(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


def derived(p):
    run = (p["W"] - p["FLAT"]) / 2               # horizontal run of one slope
    peak = run * math.tan(math.radians(p["ROOF_DEG"]))
    slope = run / math.cos(math.radians(p["ROOF_DEG"]))
    return {"run": run, "peak": peak, "slope": slope, "side_depth": p["D"] - p["V"]}


class Sheet:
    def __init__(self):
        self.parts, self.width, self.height = [], 0.0, 0.0

    def ensure(self, w, h):
        self.width, self.height = max(self.width, w), max(self.height, h)

    def line(self, x1, y1, x2, y2, w=0.6, dash=None, colour="#111"):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<line x1="{f(x1)}" y1="{f(y1)}" x2="{f(x2)}" y2="{f(y2)}" '
                          f'stroke="{colour}" stroke-width="{w}"{d}/>')

    def poly(self, pts, w=0.9, closed=True, colour="#111", dash=None):
        s = " ".join(f"{f(x)},{f(y)}" for x, y in pts)
        tag = "polygon" if closed else "polyline"
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<{tag} points="{s}" fill="none" stroke="{colour}" '
                          f'stroke-width="{w}"{d}/>')

    def circle(self, cx, cy, r, w=0.8, dash=None, colour="#111"):
        d = f' stroke-dasharray="{dash}"' if dash else ""
        self.parts.append(f'<circle cx="{f(cx)}" cy="{f(cy)}" r="{f(r)}" fill="none" '
                          f'stroke="{colour}" stroke-width="{w}"{d}/>')

    def text(self, x, y, s, size=4.0, anchor="middle", bold=False, colour="#111"):
        b = ' font-weight="bold"' if bold else ""
        self.parts.append(f'<text x="{f(x)}" y="{f(y)}" font-family="DejaVu Sans,sans-serif" '
                          f'font-size="{f(size)}" text-anchor="{anchor}" fill="{colour}"{b}>'
                          f'{s}</text>')

    def leader(self, x1, y1, x2, y2, s, size=3.3):
        self.line(x1, y1, x2, y2, w=0.3, dash="1.5,1.5", colour="#555")
        self.text(x2, y2 - 1.5 if y2 < y1 else y2 + 4.2, s, size=size, colour="#222")

    def dim(self, x1, y1, x2, y2, label, offset=0.0, size=3.3):
        dx, dy = x2 - x1, y2 - y1
        ln = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / ln, dx / ln
        ax1, ay1 = x1 + nx * offset, y1 + ny * offset
        ax2, ay2 = x2 + nx * offset, y2 + ny * offset
        self.line(ax1, ay1, ax2, ay2, w=0.3)
        for px, py in ((ax1, ay1), (ax2, ay2)):
            self.line(px - nx * 1.3, py - ny * 1.3, px + nx * 1.3, py + ny * 1.3, w=0.3)
        self.text((ax1 + ax2) / 2 + nx * 2.8, (ay1 + ay2) / 2 + ny * 2.8, label, size=size)

    def svg(self):
        return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{f(self.width)}mm" '
                f'height="{f(self.height)}mm" viewBox="0 0 {f(self.width)} {f(self.height)}">\n'
                '<rect width="100%" height="100%" fill="#fff"/>\n'
                + "\n".join(self.parts) + "\n</svg>\n")


def roof_h(x, p, d):
    """Height of the roof at x: flat across the plateau, then the slopes (y up)."""
    left, right = (p["W"] - p["FLAT"]) / 2, (p["W"] + p["FLAT"]) / 2
    if left <= x <= right:
        return p["H"] + d["peak"]
    run = (left - x) if x < left else (x - right)
    return p["H"] + d["peak"] - run * math.tan(math.radians(p["ROOF_DEG"]))


def roof_normal(x, p, d):
    """Outward unit normal at x; flat on the plateau."""
    left, right = (p["W"] - p["FLAT"]) / 2, (p["W"] + p["FLAT"]) / 2
    if left <= x <= right:
        return 0.0, 1.0
    slope = math.tan(math.radians(p["ROOF_DEG"]))
    nx = slope if x > right else -slope
    ln = math.hypot(nx, 1.0)
    return nx / ln, 1.0 / ln


def front(s, ox, base, p, d):
    W, H, FLAT, V = p["W"], p["H"], p["FLAT"], p["V"]
    peak = d["peak"]
    left, right = (W - FLAT) / 2, (W + FLAT) / 2

    def X(x):
        return ox + x

    def Y(y):
        return base - y

    s.poly([(X(0), Y(0)), (X(0), Y(H)), (X(left), Y(H + peak)), (X(right), Y(H + peak)),
            (X(W), Y(H)), (X(W), Y(0))])
    # the two front panels meet at the ridge, which is also the centre of the plateau
    s.line(X(W / 2), Y(0), X(W / 2), Y(H + peak), w=0.5, dash="2.5,2")

    # grill
    gx, gy, r = W / 2, p["CTRL_Y"], p["GRILL_D"] / 2
    s.circle(X(gx), Y(gy), r, w=0.7)
    s.circle(X(gx), Y(gy), r - 2.5, w=0.3, dash="1,1.5")
    for ring_r, count in ((r - 7, 14), (r - 15, 9), (r - 23, 4)):
        for i in range(count):
            a = 2 * math.pi * i / count + 0.35
            s.circle(X(gx + ring_r * math.cos(a)), Y(gy + ring_r * math.sin(a)), 1.4, w=0.3)

    # buttons and knob
    for i in range(p["BTN_COUNT"]):
        cx = p["BTN_X"] + (i - (p["BTN_COUNT"] - 1) / 2) * (p["BTN_D"] + 6)
        s.circle(X(cx), Y(gy), p["BTN_D"] / 2, w=0.6)
    kx = p["KNOB_X"]
    s.circle(X(kx), Y(gy), p["KNOB_D"] / 2, w=0.9)
    s.circle(X(kx), Y(gy), p["KNOB_D"] / 2 - 3, w=0.3, dash="1.5,1")
    s.line(X(kx), Y(gy), X(kx), Y(gy + p["KNOB_D"] / 2 - 3))

    # spare sticks: SIDEWAYS, so the front view shows only the thin edge
    for side in (-1, 1):
        for i in range(p["SLOTS_PER_SIDE"]):
            span = d["run"] / (p["SLOTS_PER_SIDE"] + 1)
            run_pos = span * (i + 1)
            sx = left - run_pos if side < 0 else right + run_pos
            sy = roof_h(sx, p, d)
            n = roof_normal(sx, p, d)
            tx, ty = -n[1], n[0]
            w2 = p["STICK_THIN"] / 2
            tip = (sx + n[0] * p["STICK_LEN"], sy + n[1] * p["STICK_LEN"])
            s.poly([(X(sx - tx * w2), Y(sy - ty * w2)), (X(sx + tx * w2), Y(sy + ty * w2)),
                    (X(tip[0] + tx * w2), Y(tip[1] + ty * w2)),
                    (X(tip[0] - tx * w2), Y(tip[1] - ty * w2))], w=0.8)
            s.line(X(sx - p["SLOT_W"] / 2), Y(sy), X(sx + p["SLOT_W"] / 2), Y(sy),
                   w=2.0, colour="#666")

    # the playing stick: in the plateau, face on so its label reads
    peak_y = H + peak
    s.line(X(W / 2 - 7), Y(peak_y), X(W / 2 + 7), Y(peak_y), w=2.0, colour="#000")
    hw = p["STICK_WIDE"] / 2
    s.poly([(X(W / 2 - hw), Y(peak_y)), (X(W / 2 - hw), Y(peak_y + p["STICK_LEN"])),
            (X(W / 2 + hw), Y(peak_y + p["STICK_LEN"])), (X(W / 2 + hw), Y(peak_y))], w=0.9)
    s.line(X(W / 2 - hw + 3), Y(peak_y + 30), X(W / 2 + hw - 3), Y(peak_y + 30),
           w=0.4, colour="#777")

    # callouts
    s.leader(X(W / 2 + r * 0.7), Y(gy + r * 0.7), X(W / 2 + 40), Y(gy + 30), "speaker grill")
    s.leader(X(p["BTN_X"]), Y(gy + p["BTN_D"]), X(p["BTN_X"] - 14), Y(gy + 22), "buttons")
    s.leader(X(kx), Y(gy + p["KNOB_D"] / 2), X(kx + 12), Y(gy + 20), "volume knob")
    s.leader(X(right + 12), Y(roof_h(right + 12, p, d) + 26), X(right + 30), Y(H + peak + 26),
             "spare sticks, edge-on")
    s.leader(X(W / 2 + hw - 2), Y(peak_y + 34), X(W / 2 + 34), Y(peak_y + 48),
             "playing stick, face-on")

    # dimensions
    s.dim(X(0), Y(0) + 10, X(W), Y(0) + 10, f"W {W:.0f}")
    s.dim(X(0) - 10, Y(0), X(0) - 10, Y(H), f"H {H:.0f}")
    s.dim(X(W) + 10, Y(H), X(W) + 10, Y(H + peak), f"peak {peak:.0f}")
    s.dim(X(left), Y(H + peak) + 6, X(right), Y(H + peak) + 6, f"flat {FLAT:.0f}", offset=-6)
    s.text(X(W / 2), Y(0) + 22, "FRONT ELEVATION", size=4.8, bold=True)
    s.ensure(X(W) + 30, base + 30)


def plan(s, ox, oy, p, d):
    W, D, V, FLAT = p["W"], p["D"], p["V"], p["FLAT"]
    side_depth = d["side_depth"]
    # back face at oy, front corners at oy + side_depth, ridge apex at oy + D
    s.poly([(ox, oy), (ox + W, oy), (ox + W, oy + side_depth), (ox + W / 2, oy + D),
            (ox, oy + side_depth)])
    s.line(ox + W / 2, oy + D, ox + W / 2, oy, w=0.5, dash="2.5,2")
    # plateau strip
    s.line(ox + (W - FLAT) / 2, oy, ox + (W - FLAT) / 2, oy + D, w=0.45, dash="1.5,2")
    s.line(ox + (W + FLAT) / 2, oy, ox + (W + FLAT) / 2, oy + D, w=0.45, dash="1.5,2")
    # slot positions, running front to back with the sticks' thin side facing front
    for side in (-1, 1):
        for i in range(p["SLOTS_PER_SIDE"]):
            span = d["run"] / (p["SLOTS_PER_SIDE"] + 1)
            x = W / 2 + side * ((W - FLAT) / 2 - 0) - 0
            x = (W - FLAT) / 2 - span * (i + 1) if side < 0 else (W + FLAT) / 2 + span * (i + 1)
            s.line(ox + x, oy + 8, ox + x, oy + D - 8, w=0.3, dash="1.2,2.5")
    s.dim(ox, oy + D + 10, ox + W, oy + D + 10, f"W {W:.0f}")
    s.dim(ox + W + 10, oy, ox + W + 10, oy + D, f"D {D:.0f}")
    s.text(ox + W / 2, oy + D + 20,
           "PLAN - back edge at the top, front V at the bottom, plateau dashed", size=4.4, bold=True)
    s.ensure(ox + W + 26, oy + D + 26)


def side(s, ox, base, p, d):
    H, D, PEAK, FLAT = p["H"], p["D"], d["peak"], p["FLAT"]
    side_depth = d["side_depth"]
    s.poly([(ox, base), (ox + side_depth, base), (ox + side_depth, base - H),
            (ox, base - H)])
    s.line(ox, base - H - PEAK, ox + D, base - H - PEAK, w=0.45, dash="2.5,2")
    s.text(ox + D / 2, base - H - PEAK - 4, "plateau", size=3.0, colour="#555")
    # spare sticks, seen edge-on from the side: full length visible
    for i in range(p["SLOTS_PER_SIDE"]):
        sx = ox + 16 + i * 22
        s.poly([(sx, base - H), (sx, base - H - 30), (sx + 12, base - H - 30),
                (sx + 12, base - H)], w=0.5, colour="#888", dash="2,1.5")
    s.dim(ox, base + 10, ox + side_depth, base + 10, f"side depth {side_depth:.0f}")
    s.dim(ox + D + 8, base, ox + D + 8, base - H - PEAK, f"{H + PEAK:.0f}")
    s.text(ox + D / 2, base + 20, "SIDE", size=4.4, bold=True)
    s.ensure(ox + D + 26, base + 26)


def top_plates(s, ox, oy, p, d):
    """The two roof plates and the flat plateau, laid flat as they are cut."""
    L = d["slope"]
    depth = p["D"]
    s.text(ox + L, oy - 8, "TOP PLATES (laid flat)", size=4.4, bold=True)
    # roof plate, left: trapezoid, front edge recedes by V toward the eave
    s.poly([(ox, oy), (ox + L, oy + p["V"]), (ox + L, oy + p["V"] + d["side_depth"]),
            (ox, oy + depth)])
    for i in range(p["SLOTS_PER_SIDE"]):
        along = L / (p["SLOTS_PER_SIDE"] + 1) * (i + 1)
        cx = ox + along
        cy = oy + p["V"] * (along / L) + depth / 2
        s.line(cx - p["SLOT_W"] / 2, cy, cx + p["SLOT_W"] / 2, cy, w=2.0, colour="#666")
    s.text(ox + L / 2, oy + depth + 14, f"roof L - slope {L:.0f}", size=3.4)
    # roof plate, right: mirrored
    ox2 = ox + L + 26
    s.poly([(ox2, oy + p["V"]), (ox2 + L, oy), (ox2 + L, oy + depth),
            (ox2, oy + p["V"] + d["side_depth"])])
    for i in range(p["SLOTS_PER_SIDE"]):
        along = L / (p["SLOTS_PER_SIDE"] + 1) * (i + 1)
        cx = ox2 + L - along                       # mirrored: measure from the ridge end
        cy = oy + p["V"] * (along / L) + depth / 2
        s.line(cx - p["SLOT_W"] / 2, cy, cx + p["SLOT_W"] / 2, cy, w=2.0, colour="#666")
    s.text(ox2 + L / 2, oy + depth + 14, f"roof R - slope {L:.0f}", size=3.4)
    # plateau plate
    ox3 = ox
    oy3 = oy + depth + 26
    s.poly([(ox3, oy3), (ox3 + p["FLAT"], oy3), (ox3 + p["FLAT"], oy3 + depth),
            (ox3, oy3 + depth)])
    s.line(ox3 + p["FLAT"] / 2 - 7, oy3 + depth / 2, ox3 + p["FLAT"] / 2 + 7, oy3 + depth / 2,
           w=2.0, colour="#000")
    s.text(ox3 + p["FLAT"] / 2, oy3 + depth + 14,
           f"plateau {p['FLAT']:.0f} x {depth:.0f}", size=3.4)
    s.ensure(ox2 + L + 10, oy3 + depth + 22)


def main():
    p, d = P, None
    d = derived(p)
    s = Sheet()
    base = 24 + p["H"] + d["peak"] + 54

    front(s, MARGIN, base, p, d)
    side(s, MARGIN + p["W"] + GAP + 16, base, p, d)
    plan(s, MARGIN, base + 46, p, d)
    top_plates(s, MARGIN + p["W"] + GAP + 16, base + 46 + 20, p, d)

    s.text(s.width / 2, 12,
           f"Music box enclosure - mockup - {p['T']:g} mm plywood - roof {p['ROOF_DEG']:g} deg "
           f"- mm", size=5.0, bold=True)

    out = Path(__file__).with_name("mockup.svg")
    out.write_text(s.svg(), encoding="utf-8")
    print(f"wrote {out}  ({s.width:.0f} x {s.height:.0f} mm)")
    print(f"  derived: run {d['run']:.1f}, peak {d['peak']:.1f}, slope {d['slope']:.1f}, "
          f"side depth {d['side_depth']:.1f}")


if __name__ == "__main__":
    main()
