#!/usr/bin/env python3
"""N9: draws the CatArena.exe icon (original artwork: a brass cat head with three claw slashes on a
charcoal plate, in the menu palette) and writes native/resources/cat_arena.ico (16-256 px) plus a
256 px PNG preview. Needs Pillow.  Usage: python3 tools/n9/make_icon.py [out_dir]"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

S = 1024  # drawn large, scaled down per icon size
BRASS = (214, 176, 112, 255)
BRASS_DARK = (138, 104, 58, 255)
PLATE = (24, 26, 30, 255)
PLATE_EDGE = (92, 84, 70, 255)
SLASH = (196, 58, 44, 255)


def draw():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    m = 40
    # cut-corner plate (the menu buttons' shape)
    c = 150
    plate = [(m + c, m), (S - m - c, m), (S - m, m + c), (S - m, S - m - c), (S - m - c, S - m), (m + c, S - m), (m, S - m - c), (m, m + c)]
    d.polygon(plate, fill=PLATE, outline=PLATE_EDGE, width=22)
    # cat head: ears + rounded face
    cx, cy = S / 2, S * 0.57
    r = S * 0.27
    ears = [
        [(cx - r * 1.02, cy - r * 0.25), (cx - r * 0.86, cy - r * 1.55), (cx - r * 0.18, cy - r * 0.86)],
        [(cx + r * 1.02, cy - r * 0.25), (cx + r * 0.86, cy - r * 1.55), (cx + r * 0.18, cy - r * 0.86)],
    ]
    for e in ears:
        d.polygon(e, fill=BRASS)
    d.ellipse([cx - r * 1.08, cy - r * 0.98, cx + r * 1.08, cy + r * 0.92], fill=BRASS)
    # inner ears
    for sgn in (-1, 1):
        d.polygon([(cx + sgn * r * 0.86, cy - r * 0.42), (cx + sgn * r * 0.80, cy - r * 1.22), (cx + sgn * r * 0.36, cy - r * 0.82)], fill=BRASS_DARK)
    # eyes: narrow slits
    for sgn in (-1, 1):
        ex = cx + sgn * r * 0.43
        d.polygon([(ex - r * 0.26, cy - r * 0.12), (ex + r * 0.26, cy - r * 0.12 - sgn * r * 0.1), (ex, cy + r * 0.07)], fill=PLATE)
    # nose
    d.polygon([(cx - r * 0.11, cy + r * 0.28), (cx + r * 0.11, cy + r * 0.28), (cx, cy + r * 0.4)], fill=PLATE)
    # three claw slashes across the face, lower left to upper right
    slash = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    sd = ImageDraw.Draw(slash)
    for i in range(3):
        off = (i - 1) * S * 0.085
        x0, y0 = S * 0.22 + off, S * 0.80 + off * 0.3
        x1, y1 = S * 0.70 + off, S * 0.26 + off * 0.3
        ang = math.atan2(y1 - y0, x1 - x0)
        nx, ny = -math.sin(ang), math.cos(ang)
        w = S * 0.026
        sd.polygon([(x0, y0), (x0 + (x1 - x0) * 0.5 + nx * w, y0 + (y1 - y0) * 0.5 + ny * w), (x1, y1), (x0 + (x1 - x0) * 0.5 - nx * w, y0 + (y1 - y0) * 0.5 - ny * w)], fill=SLASH)
    img.alpha_composite(slash)
    return img


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "native", "resources")
    os.makedirs(out, exist_ok=True)
    big = draw()
    sizes = [256, 128, 64, 48, 32, 24, 16]
    frames = []
    for s in sizes:
        im = big.resize((s, s), Image.LANCZOS)
        if s <= 32:
            im = im.filter(ImageFilter.UnsharpMask(radius=1, percent=60, threshold=2))
        frames.append(im)
    frames[0].save(os.path.join(out, "cat_arena.ico"), sizes=[(s, s) for s in sizes], append_images=frames[1:])
    frames[0].save(os.path.join(out, "cat_arena_icon_256.png"))
    print("wrote", os.path.join(out, "cat_arena.ico"))


if __name__ == "__main__":
    main()
