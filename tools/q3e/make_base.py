#!/usr/bin/env python3
"""N10-Q3E stage B: generates the Cat Arena base folder for the Quake3e engine (game/base/catarena):

  gfx/2d/bigchars.png           console / HUD / menu charset (16x16 grid) drawn from the shipped
                                Liberation Mono Bold font (assets/n7/fonts, SIL OFL)
  textures/catarena/*.png       procedural test-arena textures (original, generated here)
  scripts/catarena.shader       shaders: white, caulk, the test textures, the light panel
  scripts/shaderlist.txt
  maps/ca_test.map              the stage-B test arena in Quake III units (brushes + entities)
  default.cfg                   engine defaults (native modules, controls, window)

Nothing here comes from id Software. The map is compiled by tools/q3e/build_maps.sh (q3map2).
Needs Pillow. Usage: python3 tools/q3e/make_base.py [repo_root]
"""
import os
import random
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", ".."))
OUT = os.path.join(ROOT, "game", "base", "catarena")
FONT = os.path.join(ROOT, "assets", "n7", "fonts", "LiberationMono-Bold.ttf")


def out(*parts):
    p = os.path.join(OUT, *parts)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    return p


# ---- charset ------------------------------------------------------------------------------------
def make_charset():
    cell, n = 32, 16                       # 512 x 512, 16 x 16 cells, character code = row * 16 + col
    img = Image.new("RGBA", (cell * n, cell * n), (255, 255, 255, 0))
    font = ImageFont.truetype(FONT, 28)
    mask = Image.new("L", img.size, 0)
    d = ImageDraw.Draw(mask)
    for code in range(32, 127):
        col, row = code % n, code // n
        ch = chr(code)
        l, t, r, b = d.textbbox((0, 0), ch, font=font)
        x = col * cell + (cell - (r - l)) / 2 - l
        y = row * cell + (cell - 28) / 2 - 2
        d.text((x, y), ch, fill=255, font=font)
    # block glyph for the cursor / boxes at 11 (Quake III uses 11 for the console cursor block)
    d.rectangle([11 % n * cell + 4, 0 * cell + 4, 11 % n * cell + cell - 5, cell - 5], fill=255)
    # soft dark outline so text reads on bright surfaces
    shadow = mask.filter(ImageFilter.MaxFilter(3))
    a = Image.new("RGBA", img.size, (0, 0, 0, 0))
    a.putalpha(shadow.point(lambda v: v * 3 // 4))
    img = Image.alpha_composite(a, Image.merge("RGBA", (*Image.new("RGB", img.size, (255, 255, 255)).split(), mask)))
    img.save(out("gfx", "2d", "bigchars.png"))


# ---- textures -----------------------------------------------------------------------------------
def noise_tile(size, base, amp, seed):
    rnd = random.Random(seed)
    img = Image.new("RGB", (size, size), base)
    px = img.load()
    for y in range(size):
        for x in range(size):
            n = rnd.randint(-amp, amp)
            r, g, b = base
            px[x, y] = (max(0, min(255, r + n)), max(0, min(255, g + n)), max(0, min(255, b + n)))
    return img.filter(ImageFilter.GaussianBlur(0.6))


def floor_plate():
    img = noise_tile(256, (62, 64, 68), 9, 1)
    d = ImageDraw.Draw(img)
    for i in range(0, 256, 128):                 # 2 x 2 plates with bevelled seams
        d.line([(i, 0), (i, 255)], fill=(30, 31, 34), width=3)
        d.line([(0, i), (255, i)], fill=(30, 31, 34), width=3)
        d.line([(i + 2, 0), (i + 2, 255)], fill=(92, 94, 98), width=1)
        d.line([(0, i + 2), (255, i + 2)], fill=(92, 94, 98), width=1)
    for cx in (12, 116, 140, 244):                # rivets
        for cy in (12, 116, 140, 244):
            d.ellipse([cx - 3, cy - 3, cx + 3, cy + 3], fill=(104, 104, 100), outline=(36, 36, 38))
    for y in range(20, 108, 8):                   # tread pattern on one plate
        d.line([(150, y), (234, y + 8)], fill=(74, 76, 80), width=2)
    img.save(out("textures", "catarena", "floor_plate.png"))


def wall_panel():
    img = noise_tile(256, (88, 80, 72), 10, 2)
    d = ImageDraw.Draw(img)
    for y in range(0, 256, 64):                   # stone courses, staggered joints
        d.line([(0, y), (255, y)], fill=(48, 43, 39), width=3)
        off = 0 if (y // 64) % 2 == 0 else 64
        for x in range(off, 256 + 128, 128):
            d.line([(x % 256, y), (x % 256, y + 63)], fill=(48, 43, 39), width=3)
    img.save(out("textures", "catarena", "wall_panel.png"))


def trim_brass():
    img = noise_tile(128, (150, 118, 66), 8, 3)
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, 127, 5], fill=(214, 176, 112))
    d.rectangle([0, 122, 127, 127], fill=(96, 72, 38))
    for x in range(-128, 128, 24):                # claw-slash stripes, the Cat Arena motif
        d.polygon([(x, 110), (x + 10, 110), (x + 34, 18), (x + 24, 18)], fill=(118, 40, 30))
    img.save(out("textures", "catarena", "trim_brass.png"))


def pillar_metal():
    img = noise_tile(128, (70, 76, 84), 7, 4)
    d = ImageDraw.Draw(img)
    for x in range(0, 128, 32):
        d.line([(x, 0), (x, 127)], fill=(40, 44, 50), width=2)
        d.line([(x + 2, 0), (x + 2, 127)], fill=(104, 110, 118), width=1)
    img.save(out("textures", "catarena", "pillar_metal.png"))


def ceiling_grate():
    img = Image.new("RGB", (128, 128), (26, 27, 30))
    d = ImageDraw.Draw(img)
    for i in range(0, 128, 16):
        d.line([(i, 0), (i, 127)], fill=(58, 60, 64), width=3)
        d.line([(0, i), (127, i)], fill=(58, 60, 64), width=3)
    img.save(out("textures", "catarena", "ceiling_grate.png"))


def light_panel():
    img = Image.new("RGB", (64, 64), (60, 54, 44))
    d = ImageDraw.Draw(img)
    d.rectangle([6, 6, 57, 57], fill=(255, 236, 196))
    d.rectangle([6, 6, 57, 57], outline=(150, 118, 66), width=3)
    img.save(out("textures", "catarena", "light_panel.png"))


def fx_textures():
    """gfx/catarena/glow.png (soft round sprite) and beam.png (soft line across V): white with
    alpha, coloured per weapon by vertex colour (stage D part 3 weapon effects)."""
    import math
    g = Image.new("RGBA", (64, 64))
    px = g.load()
    for y in range(64):
        for x in range(64):
            r = math.hypot(x - 31.5, y - 31.5) / 32.0
            a = max(0.0, 1.0 - r) ** 2
            px[x, y] = (255, 255, 255, int(round(255 * a)))
    g.save(out("gfx", "catarena", "glow.png"))
    b = Image.new("RGBA", (8, 32))
    px = b.load()
    for y in range(32):
        t = (y - 15.5) / 16.0
        a = math.exp(-t * t * 9.0)
        for x in range(8):
            px[x, y] = (255, 255, 255, int(round(255 * a)))
    b.save(out("gfx", "catarena", "beam.png"))


def medkit_texture():
    """textures/catarena/medkit.png: N10's medkit (world atlas tile 11, MATERIAL.MEDKIT) as the
    legacy draws it (dynamic.frag: alpha cut-out at 0.36, flat light 0.896 x 0.80 x 1.19 = 0.853, no
    other term without dynamic lights): the cut-out and the light are baked in, so the shader can be
    rgbGen identity and alphaFunc GT0."""
    atlas = Image.open(os.path.join(ROOT, "assets", "n2", "world-atlas-runtime.png")).convert("RGBA")
    tile = atlas.crop((3 * 128, 2 * 128, 4 * 128, 3 * 128))   # tile m at (m % 4, m // 4)
    px = tile.load()
    for y in range(128):
        for x in range(128):
            r, g, b, a = px[x, y]
            k = (.69 + .33 * .625) * .80 * 1.19
            px[x, y] = (int(round(r * k)), int(round(g * k)), int(round(b * k)), 255 if a >= .36 * 255 else 0)
    tile.save(out("textures", "catarena", "medkit.png"))


def crosshairs():
    """gfx/2d/crosshaira..j: ten simple original crosshairs (cg_drawCrosshair 1-10)."""
    for i, ch in enumerate("abcdefghij"):
        img = Image.new("RGBA", (64, 64), (255, 255, 255, 0))
        d = ImageDraw.Draw(img)
        w = (255, 255, 255, 255)
        gap = 6 + (i % 3) * 3
        if i in (0, 3, 6, 9):
            d.ellipse([29, 29, 34, 34], fill=w)                                  # centre dot
        if i in (1, 2, 3, 4, 5, 7, 8):                                          # four ticks
            for x0, y0, x1, y1 in ((31, 4, 32, 31 - gap), (31, 32 + gap, 32, 59), (4, 31, 31 - gap, 32), (32 + gap, 31, 59, 32)):
                d.rectangle([x0, y0, x1, y1], fill=w)
        if i in (5, 6, 9):
            d.ellipse([14, 14, 49, 49], outline=w, width=2)                     # ring
        if i in (7, 8):                                                         # claw marks
            for k in (-8, 0, 8):
                d.line([(22 + k, 46), (34 + k, 18)], fill=(255, 255, 255, 200), width=2)
        img.save(out("gfx", "2d", "crosshair" + ch + ".png"))


SHADERS = """// Cat Arena: Feral Claw — shaders of the stage-B base folder (generated by tools/q3e/make_base.py)

white
{
	{
		map *white
		blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA
		rgbGen vertex
	}
}

catarena/fx/glow
{
	nopicmip
	cull none
	sort additive
	{
		map gfx/catarena/glow.png
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

catarena/fx/beam
{
	nopicmip
	cull none
	sort additive
	{
		clampmap gfx/catarena/beam.png
		blendFunc GL_SRC_ALPHA GL_ONE
		rgbGen vertex
		alphaGen vertex
	}
}

catarena/medkit
{
	nopicmip
	cull none
	{
		map textures/catarena/medkit.png
		alphaFunc GT0
		rgbGen identity
		depthWrite
	}
}

textures/common/playerclip
{
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nonsolid
	surfaceparm trans
	surfaceparm nomarks
	surfaceparm noimpact
	surfaceparm playerclip
}

textures/common/trigger
{
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nonsolid
	surfaceparm trigger
}

textures/common/caulk
{
	surfaceparm nodraw
	surfaceparm nolightmap
	surfaceparm nomarks
}

textures/catarena/light_panel
{
	qer_editorimage textures/catarena/light_panel.png
	q3map_surfacelight 900
	q3map_lightRGB 1.0 0.92 0.78
	surfaceparm nomarks
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/catarena/light_panel.png
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
	{
		map textures/catarena/light_panel.png
		blendFunc GL_ONE GL_ONE
	}
}

textures/catarena/trim_brass
{
	qer_editorimage textures/catarena/trim_brass.png
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/catarena/trim_brass.png
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen identity
	}
	{
		map textures/catarena/trim_brass.png
		blendFunc GL_ONE GL_ONE
		rgbGen wave sin 0.05 0.05 0 0.25
	}
}
"""


# ---- test arena (.map, Quake III units) ---------------------------------------------------------
def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def face(p0, p1, p2, inside, tex, scale=0.5):
    # q3map2: normal = (p2 - p0) x (p1 - p0), pointing out of the brush
    n = cross(sub(p2, p0), sub(p1, p0))
    if dot(n, sub(inside, p0)) > 0:
        p1, p2 = p2, p1
    f = lambda p: "( %d %d %d )" % p
    return "%s %s %s %s 0 0 0 %g %g 0 0 0" % (f(p0), f(p1), f(p2), tex, scale, scale)


def box(x0, y0, z0, x1, y1, z1, tex, top=None, scale=0.5):
    c = ((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2)
    top = top or tex
    faces = [
        face((x0, y0, z0), (x0, y1, z0), (x0, y0, z1), c, tex, scale),
        face((x1, y0, z0), (x1, y1, z0), (x1, y0, z1), c, tex, scale),
        face((x0, y0, z0), (x1, y0, z0), (x0, y0, z1), c, tex, scale),
        face((x0, y1, z0), (x1, y1, z0), (x0, y1, z1), c, tex, scale),
        face((x0, y0, z0), (x1, y0, z0), (x0, y1, z0), c, tex, scale),
        face((x0, y0, z1), (x1, y0, z1), (x0, y1, z1), c, top, scale),
    ]
    return "{\n" + "\n".join(faces) + "\n}\n"


def ramp(x0, y0, x1, y1, z0, h, tex, top):
    """Wedge rising along +x from z0 at x0 to z0+h at x1."""
    c = (x0 + (x1 - x0) * 0.7, (y0 + y1) / 2, z0 + h * 0.3)
    faces = [
        face((x0, y0, z0), (x1, y0, z0), (x0, y1, z0), c, tex),                 # bottom
        face((x1, y0, z0), (x1, y1, z0), (x1, y0, z0 + h), c, tex),             # high end
        face((x0, y0, z0), (x1, y0, z0), (x1, y0, z0 + h), c, tex),             # side y0
        face((x0, y1, z0), (x1, y1, z0), (x1, y1, z0 + h), c, tex),             # side y1
        face((x0, y0, z0), (x0, y1, z0), (x1, y0, z0 + h), c, top),             # slope
    ]
    return "{\n" + "\n".join(faces) + "\n}\n"


def make_map():
    F, W, T, P, C, L = ("catarena/floor_plate", "catarena/wall_panel", "catarena/trim_brass",
                        "catarena/pillar_metal", "catarena/ceiling_grate", "catarena/light_panel")
    S, H, K = 768, 512, 32          # half size, height, wall thickness
    b = []
    b.append(box(-S - K, -S - K, -K, S + K, S + K, 0, F))                      # floor
    b.append(box(-S - K, -S - K, H, S + K, S + K, H + K, C))                   # ceiling
    b.append(box(-S - K, -S - K, 0, -S, S + K, H, W))                          # walls
    b.append(box(S, -S - K, 0, S + K, S + K, H, W))
    b.append(box(-S, -S - K, 0, S, -S, H, W))
    b.append(box(-S, S, 0, S, S + K, H, W))
    for x0, y0, x1, y1 in ((-S, -S, S, -S + 16), (-S, S - 16, S, S), (-S, -S + 16, -S + 16, S - 16), (S - 16, -S + 16, S, S - 16)):
        b.append(box(x0, y0, 0, x1, y1, 24, T))                               # brass skirting
    for px, py in ((-384, -384), (384, -384), (-384, 384), (384, 384)):       # pillars
        b.append(box(px - 32, py - 32, 0, px + 32, py + 32, H, P))
    # platform 128 high along the north wall, reached by a ramp (west) and stairs (east)
    b.append(box(-256, 448, 0, 256, S, 128, W, top=F))
    b.append(box(-256, 432, 112, 256, 448, 128, T))                            # brass lip
    b.append(ramp(-640, 448, -256, S, 0, 128, W, F))
    for i in range(8):                                                          # 8 steps of 16 u
        b.append(box(256 + i * 32, 448, 0, 256 + (i + 1) * 32, S, 128 - i * 16, W, top=F))
    # low cover blocks and a jump-height crate
    b.append(box(-96, -160, 0, 96, -128, 48, T))
    b.append(box(-32, 96, 0, 32, 160, 40, P))
    # ceiling light panels
    for lx, ly in ((-384, 0), (384, 0), (0, -384), (0, 384), (0, 0)):
        b.append(box(lx - 64, ly - 64, H - 8, lx + 64, ly + 64, H, L))
    world = '{\n"classname" "worldspawn"\n"message" "Cat Arena test arena (N10-Q3E stage B)"\n"_color" "1 0.95 0.88"\n"_ambient" "12"\n' + "".join(b) + "}\n"
    ents = []
    for i, (x, y, a) in enumerate(((0, -560, 90), (-560, 0, 0), (560, 0, 180), (0, 200, 270))):
        ents.append('{\n"classname" "info_player_deathmatch"\n"origin" "%d %d 32"\n"angle" "%d"\n}\n' % (x, y, a))
    for x, y in ((-640, -640), (640, -640), (-640, -200), (640, -200), (-640, 300), (640, 300), (-200, -640), (200, -640)):
        ents.append('{\n"classname" "info_ca_enemy_spawn"\n"origin" "%d %d 32"\n}\n' % (x, y))   # Cat Arena enemies (stage C)
    for x, y, z in ((-560, -560, 256), (560, -560, 256), (-560, 560, 320), (560, 560, 320), (0, 600, 256)):
        ents.append('{\n"classname" "light"\n"origin" "%d %d %d"\n"light" "450"\n"_color" "1 0.8 0.55"\n}\n' % (x, y, z))
    with open(out("maps", "ca_test.map"), "w", newline="\n") as f:
        f.write("// Cat Arena test arena, generated by tools/q3e/make_base.py (Quake III units, 32 u = 1 m)\n")
        f.write(world + "".join(ents))


DEFAULT_CFG = """// Cat Arena: Feral Claw — engine defaults (N10-Q3E, generated by tools/q3e/make_base.py)
// SHIFT sprint (+button5), C / CTRL crouch (slide when sprinting), R reload (+button6),
// Q switch weapon (+button7)
// Native game modules only (the Cat Arena modules are C++20; QVM cannot run them).
seta vm_game "0"
seta vm_cgame "0"
seta vm_ui "0"
seta cl_renderer "vulkan"
seta com_introPlayed "1"
seta sv_pure "0"
seta bot_enable "0"
seta fraglimit "0"
seta timelimit "0"
seta name "Cat"
seta cl_run "1"
seta sensitivity "5"
seta r_mode "-1"
seta r_customwidth "1280"
seta r_customheight "720"
seta r_fullscreen "0"
seta r_vbo "1"
seta cg_drawFPS "1"
unbindall
bind w "+forward"
bind s "+back"
bind a "+moveleft"
bind d "+moveright"
bind SPACE "+moveup"
bind c "+movedown"
bind CTRL "+movedown"
bind SHIFT "+button5"
bind q "+button7"
// creatures are drawn as polygons (posed every frame): room for 20 live ones and 24 bodies
seta r_maxpolys "16384"
seta r_maxpolyverts "65536"
// N10 audio defaults (MixSettings): master 70 % = s_volume; music 46 % x master = s_musicvolume;
// the weapon (85 %) and enemy (80 %) bus levels are baked into the sounds (tools/q3e/sound_bake.cpp)
seta s_volume "0.7"
seta s_musicvolume "0.322"
seta s_khz "44"
seta ca_music "1"
bind MOUSE1 "+attack"
bind r "+button6"
bind TAB "+scores"
bind ESCAPE "togglemenu"
bind ` "toggleconsole"
bind F12 "screenshotJPEG"
"""


def main():
    make_charset()
    crosshairs()
    floor_plate()
    wall_panel()
    trim_brass()
    pillar_metal()
    ceiling_grate()
    light_panel()
    fx_textures()
    medkit_texture()
    with open(out("scripts", "catarena.shader"), "w", newline="\n") as f:
        f.write(SHADERS)
    listed = []   # keep the entries other tools added (the map converters add their shader files)
    if os.path.exists(out("scripts", "shaderlist.txt")):
        listed = [l.strip() for l in open(out("scripts", "shaderlist.txt")) if l.strip()]
    if "catarena" not in listed:
        listed.insert(0, "catarena")
    with open(out("scripts", "shaderlist.txt"), "w", newline="\n") as f:
        f.write("".join(n + "\n" for n in listed))
    make_map()
    with open(out("default.cfg"), "w", newline="\n") as f:
        f.write(DEFAULT_CFG)
    print("wrote", OUT)


if __name__ == "__main__":
    main()
