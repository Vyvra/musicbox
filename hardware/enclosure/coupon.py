#!/usr/bin/env python3
"""Joint fit coupon: three L-corners, each cut for a different kerf guess.

A laser removes half the kerf from each side of a cut, so a slot drawn at nominal width w comes
out w + kerf while a tab drawn at w comes out w - kerf. To land a snug fit when the machine's
kerf is k, cut the slot at (w - k) and leave the tab at w.

Nobody's published kerf figure is reliable for a given machine and sheet, so cut this coupon
first, push each pair together, and keep whichever fits. The panel file is then generated for
that figure.

    python3 coupon.py            # writes joint-test.svg
"""
from pathlib import Path

T = 4.0            # plywood thickness
TAB_W = 12.0       # nominal tab width
N_TABS = 3
PIECE = 90.0       # coupon length
SLOT_INSET = T     # slots sit one thickness in from the edge
KERFS = (0.10, 0.15, 0.20)


def f(v):
    return f"{v:.3f}".rstrip("0").rstrip(".")


def tabbed_piece(x0, y0, length, height, tab_w, n):
    """Horizontal piece, tabs protruding downward by T along its bottom edge."""
    pts = [(x0, y0)]
    pitch = length / n
    # walk the top edge left to right, rising into each tab
    for i in range(n):
        centre = x0 + pitch * (i + 0.5)
        tab_l, tab_r = centre - tab_w / 2, centre + tab_w / 2
        pts += [(tab_l, y0), (tab_l, y0 - T), (tab_r, y0 - T), (tab_r, y0)]
    pts += [(x0 + length, y0), (x0 + length, y0 + height), (x0, y0 + height)]
    return pts


def slotted_piece(x0, y0, length, height, tab_w, n, kerf):
    """Vertical piece with through-slots, inset one thickness from its top edge."""
    body = [(x0, y0), (x0 + length, y0), (x0 + length, y0 - height), (x0, y0 - height)]
    slots = []
    pitch = length / n
    slot_w = tab_w - kerf
    slot_h = T + kerf
    for i in range(n):
        centre = x0 + pitch * (i + 0.5)
        slots.append((centre - slot_w / 2, y0 - SLOT_INSET - slot_h / 2, slot_w, slot_h))
    return body, slots


def main():
    parts, labels = [], []
    ox = 20.0
    base_y = 74.0                       # slotted piece: bottom edge sits here
    for k in KERFS:
        # slotted piece on top, tabbed piece below it: the tabs point into the slots
        body, slots = slotted_piece(ox, base_y, PIECE, 26.0, TAB_W, N_TABS, k)
        parts.append(f'<polygon points="{" ".join(f"{f(x)},{f(y)}" for x, y in body)}" '
                     f'fill="none" stroke="#000" stroke-width="0.3"/>')
        for sx, sy, sw, sh in slots:
            parts.append(f'<rect x="{f(sx)}" y="{f(sy)}" width="{f(sw)}" height="{f(sh)}" '
                         f'fill="none" stroke="#000" stroke-width="0.3"/>')

        tabbed = tabbed_piece(ox, base_y + 26.0, PIECE, 20.0, TAB_W, N_TABS)
        parts.append(f'<polygon points="{" ".join(f"{f(x)},{f(y)}" for x, y in tabbed)}" '
                     f'fill="none" stroke="#000" stroke-width="0.3"/>')

        labels.append(f'<text x="{f(ox + PIECE / 2)}" y="{f(base_y + 56)}" '
                      f'font-family="DejaVu Sans,sans-serif" font-size="4" text-anchor="middle" '
                      f'fill="#000">kerf {k:.2f} mm</text>')
        labels.append(f'<text x="{f(ox + PIECE / 2)}" y="{f(base_y + 61)}" '
                      f'font-family="DejaVu Sans,sans-serif" font-size="3" text-anchor="middle" '
                      f'fill="#666">slot {TAB_W - k:.2f} wide</text>')
        ox += PIECE + 22.0

    width = ox + 4
    height = base_y + 78

    header = [
        f'<text x="20" y="14" font-family="DejaVu Sans,sans-serif" font-size="5" fill="#000">'
        f'joint fit coupon &#183; {T:g} mm plywood &#183; cut, push together, keep the fit</text>',
        f'<text x="20" y="21" font-family="DejaVu Sans,sans-serif" font-size="3.2" fill="#555">'
        f'tab {TAB_W:g} wide, protruding {T:g} mm; slots inset {SLOT_INSET:g} mm from the edge '
        f'and cut narrow by the stated kerf</text>',
        f'<line x1="20" y1="{f(height - 12)}" x2="120" y2="{f(height - 12)}" stroke="#000" '
        f'stroke-width="0.3"/>',
        f'<text x="20" y="{f(height - 6)}" font-family="DejaVu Sans,sans-serif" font-size="3.6" '
        f'fill="#000">print at 100% &#8212; this line must measure 100.0 mm</text>',
    ]

    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{f(width)}mm" height="{f(height)}mm" '
           f'viewBox="0 0 {f(width)} {f(height)}">\n<rect width="100%" height="100%" '
           f'fill="#fff"/>\n' + "\n".join(header + parts + labels) + "\n</svg>\n")
    out = Path(__file__).with_name("joint-test.svg")
    out.write_text(svg, encoding="utf-8")
    print(f"wrote {out}  ({width:.0f} x {height:.0f} mm)")


if __name__ == "__main__":
    main()
