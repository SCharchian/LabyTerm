#!/usr/bin/env python3
"""Generate the LabyTerm icons (packaging/icons/*.svg, then PNGs via rsvg-convert).

Palette: black, white and one orange (the doorway and the moon). Thin white line work on a black tile.

    tools/make_icons.py            # writes the SVGs and the PNG sizes
    tools/make_icons.py --svg-only
"""
import random, subprocess, sys
from pathlib import Path

OUT = Path(__file__).resolve().parent.parent / "packaging" / "icons"
BLACK, WHITE, ORANGE = "#000000", "#ffffff", "#ff6a00"
SW = 6  # line weight on the 256 canvas

# the active scheme; `scheme()` swaps it so every builder can draw dark-on-light too
BG, INK = BLACK, WHITE

def scheme(bg, ink):
    global BG, INK
    BG, INK = bg, ink

def svg(body, tile=True, tile_fill=None):
    back = f'<rect width="256" height="256" rx="48" fill="{tile_fill or BG}"/>' if tile else ""
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256">'
            f'<defs><clipPath id="t"><rect width="256" height="256" rx="48"/></clipPath></defs>'
            f'{back}<g clip-path="url(#t)" stroke-linejoin="round" stroke-linecap="round">{body}</g></svg>\n')

def stroke(d, fill=None, w=SW):
    return f'<path d="{d}" fill="{fill or BG}" stroke="{INK}" stroke-width="{w}"/>'

def crenellated(x0, x1, top, base, n, depth=12, ratio=0.7):
    """Closed outline: base-left up, n merlons across the top, down, back along the base."""
    m = (x1 - x0) / (n + (n - 1) * ratio)
    g = m * ratio
    p = f"M{x0:.1f} {base}V{top}"
    x = x0
    for i in range(n):
        x += m
        p += f"H{x:.1f}"
        if i < n - 1:
            x += g
            p += f"v{depth}H{x:.1f}V{top}"
    return p + f"V{base}z"

def door(cx, base, w, h):
    r = w / 2
    return f'<path d="M{cx - r} {base}V{base - h + r}a{r} {r} 0 0 1 {w} 0V{base}z" fill="{ORANGE}"/>'

def moon(cx=219, cy=27, r=20, line=34):
    return (f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="none" stroke="{ORANGE}" stroke-width="5"/>'
            f'<path d="M{cx - line - r} {cy + 1}H{cx + line + r}" stroke="{ORANGE}" stroke-width="2.5"/>')

def ground(y=232, x0=22, x1=234, bumps=(44, 190)):
    d = f"M{x0} {y}"
    for b in bumps:
        d += f"H{b}q9-11 18 0"
    return stroke(d + f"H{x1}", fill="none", w=SW)

def castle_body(top=60, base=232):
    return (stroke(crenellated(88, 172, top + 48, base, 5, 9))                    # keep wall (behind)
            + stroke(crenellated(40, 90, top, base, 3, 12, 0.55))                  # left tower
            + stroke(crenellated(170, 220, top, base, 3, 12, 0.55))                # right tower
            + door(130, base - 3, 44, 62) + ground(base))

def castle():       return svg(castle_body() + moon())
def castle_plain(): return svg(castle_body())                         # no moon
def castle_flat():  return svg(castle_body() + moon(), tile=False)    # no tile, for dark panels
def castle_light():                                                   # inverted tile: black lines on white
    scheme(WHITE, BLACK)
    try:    return svg(castle_body() + moon(), tile_fill=WHITE)
    finally: scheme(BLACK, WHITE)

def rain(n, seed=5, w=2.5, ink=None, slant=0.36):
    """Slanted streaks over the sky (drawn first, so the castle hides them). One per cell of a jittered
    grid (n is rounded to a square number) so the coverage is even; seeded, so the icon is stable."""
    rnd = random.Random(seed)
    g = max(2, round(n ** 0.5))
    cw, ch = 276 / g, 236 / g
    d = ""
    for i in range(g):
        for j in range(g):
            x, y, ln = -10 + (i + rnd.uniform(.15, .85)) * cw, -10 + (j + rnd.uniform(.1, .7)) * ch, rnd.uniform(16, 26)
            d += f"M{x:.0f} {y:.0f}l{-ln * slant:.1f} {ln:.1f}"
    return f'<path d="{d}" fill="none" stroke="{ink or INK}" stroke-width="{w}"/>'

def bolt(color=None, w=6, x=150):
    """A lightning bolt from the top edge down onto the keep wall."""
    pts = [(x, -4), (x - 20, 38), (x - 4, 44), (x - 26, 104)]
    return (f'<path d="M' + "L".join(f"{a} {b}" for a, b in pts) +
            f'" fill="none" stroke="{color or INK}" stroke-width="{w}" stroke-linejoin="miter" stroke-linecap="butt"/>')

def castle_rain():        return svg(rain(16) + castle_body() + moon())
def castle_downpour():    return svg(rain(36, 9, 3) + castle_body() + moon())
def castle_storm():       return svg(rain(25, 3) + castle_body() + moon() + bolt())
def castle_storm_orange(): return svg(rain(25, 3) + castle_body() + moon() + bolt(ORANGE))
def castle_flash():
    """The game's lightning flash: a lit sky, the line work gone dark, rain dark, orange door and moon stay."""
    scheme(WHITE, BLACK)
    try:    return svg(rain(25, 3) + castle_body() + moon() + bolt(BLACK), tile_fill=WHITE)
    finally: scheme(BLACK, WHITE)

def castle_rising():
    """A big orange moon rising behind the keep wall, between the towers."""
    big = f'<circle cx="130" cy="112" r="40" fill="none" stroke="{ORANGE}" stroke-width="5"/>'
    return svg(big + castle_body(top=70))

def castle_small():
    """The castle small in the tile with open sky: reads as a scene."""
    return svg(f'<g transform="translate(128 150) scale(.72) translate(-128 -146)">' + castle_body() + "</g>" + moon(196, 56, 22, 40))

def gate():
    """Close-up of the front gate: the orange arch with a portcullis, in a crenellated wall."""
    grid = (f'<path d="M104 232V150M128 232V134M152 232V150M84 182H172M84 206H172" '
            f'stroke="{BG}" stroke-width="3" fill="none"/>')
    return svg(stroke(crenellated(22, 234, 56, 232, 7, 16, 0.6)) + door(128, 232, 96, 124) + grid + moon(219, 27, 0.01, 0)[0:0])

def tower():
    return svg(stroke(crenellated(80, 176, 38, 228, 4, 16, 0.6))
               + stroke("M118 96v26M138 96v26", w=5, fill="none")
               + door(128, 225, 44, 66) + ground(228) + moon())

def maze_walls(n, seed):
    """Perfect maze by recursive backtracker from the centre (seeded, so the icon is stable)."""
    rnd = random.Random(seed)
    seen, open_ = {(n // 2, n // 2)}, set()
    stack = [(n // 2, n // 2)]
    while stack:
        q, r = stack[-1]
        nb = [(q + dq, r + dr) for dq, dr in ((1, 0), (-1, 0), (0, 1), (0, -1))
              if 0 <= q + dq < n and 0 <= r + dr < n and (q + dq, r + dr) not in seen]
        if not nb:
            stack.pop(); continue
        nxt = rnd.choice(nb)
        seen.add(nxt); open_.add(frozenset(((q, r), nxt))); stack.append(nxt)
    return open_

def maze(n=7, c=30, seed=7, with_moon=False):
    """A labyrinth drawn as thin walls, the way in at the bottom, the orange door at its heart."""
    o = (256 - n * c) / 2
    opened = maze_walls(n, seed)
    d = f"M{o} {o}H{o + n * c}V{o + n * c}H{o + (n // 2 + 1) * c}M{o + (n // 2) * c} {o + n * c}H{o}V{o}"
    for r in range(n):
        for q in range(n):
            if q + 1 < n and frozenset(((q, r), (q + 1, r))) not in opened:
                d += f"M{o + (q + 1) * c} {o + r * c}v{c}"
            if r + 1 < n and frozenset(((q, r), (q, r + 1))) not in opened:
                d += f"M{o + q * c} {o + (r + 1) * c}h{c}"
    cx, cy = o + (n // 2 + .5) * c, o + (n // 2 + 1) * c - 2
    return svg(f'<path d="{d}" fill="none" stroke="{INK}" stroke-width="{SW}"/>' + door(cx, cy, 20, 24))

def prompt():
    return svg(stroke("M26 44H230V212H26z", fill="none", w=SW)
               + stroke("M26 76H230", fill="none", w=SW)
               + "".join(f'<circle cx="{x}" cy="60" r="5" fill="{ORANGE if x == 48 else INK}"/>' for x in (48, 66, 84))
               + stroke("M56 112l32 26-32 26", fill="none", w=9)
               + door(168, 190, 44, 66))

def doorway():
    return svg(f'<g fill="none" stroke="{INK}" stroke-width="{SW}">'
               f'<rect x="30" y="30" width="196" height="196" rx="4"/>'
               f'<path d="M30 30L98 98M226 30L158 98M30 226L98 174M226 226L158 174"/>'
               f'<rect x="98" y="98" width="60" height="76"/></g>' + door(128, 174, 36, 56))

ICONS = {
    "labyterm-castle": castle,
    "labyterm-castle-plain": castle_plain,
    "labyterm-castle-flat": castle_flat,
    "labyterm-castle-light": castle_light,
    "labyterm-castle-rain": castle_rain,
    "labyterm-castle-downpour": castle_downpour,
    "labyterm-castle-storm": castle_storm,
    "labyterm-castle-storm-orange": castle_storm_orange,
    "labyterm-castle-flash": castle_flash,
    "labyterm-castle-rising": castle_rising,
    "labyterm-castle-small": castle_small,
    "labyterm-gate": gate,
    "labyterm-tower": tower,
    "labyterm-maze": maze,
    "labyterm-maze-b": lambda: maze(seed=11),
    "labyterm-prompt": prompt,
    "labyterm-doorway": doorway,
}
SIZES = [16, 24, 32, 48, 64, 128, 256, 512]

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for old in OUT.glob("*.svg"): old.unlink()
    made = {name: fn() for name, fn in ICONS.items()}
    for name, text in made.items():
        (OUT / f"{name}.svg").write_text(text)
    # the Omarchy plugin ships standalone (it is copied to its own repo), so it carries a copy of the default icon
    (OUT.parent / "omarchy-plugin" / "labyterm.svg").write_text(made["labyterm-castle-storm"])
    if "--svg-only" in sys.argv: return
    for name in made:
        for px in SIZES:
            d = OUT / "png" / f"{px}x{px}"
            d.mkdir(parents=True, exist_ok=True)
            subprocess.run(["rsvg-convert", "-w", str(px), "-h", str(px), str(OUT / f"{name}.svg"),
                            "-o", str(d / f"{name}.png")], check=True)

if __name__ == "__main__":
    main()
