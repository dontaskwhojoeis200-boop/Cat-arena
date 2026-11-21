#!/usr/bin/env python3
"""N10-Q3E stage D: converts an N10 map (assets/n1/maps/<id>: map.json + world.cmesh) into Quake III
map sources for q3map2. Layout, levels and collision are taken from the N10 data, not redrawn.

Collision (worldspawn brushes, nodraw, from map.json "compiled" = what N10's physics uses):
  slabs   one convex brush per slab: flat top at z (or the ramp plane), bottom at z - thickness or
          at the slab's base
  solids  convex prisms z0..z1 (columns, blocks, equipment)
  walls   segments -> thin brushes (0.16 m) z0..z1; detail brushes (no vis portals)
  edges   player clip along the edges of N10's footprint (stage D part 4): N10 never lets a player
          or an enemy stand outside the footprint (levelWalkable), so its open edges (bridge sides,
          drops to the void) get invisible walls that stop players and enemies but not shots
  hull    a box around the footprint: sky on top and sides, caulk below (seals the map)
Visuals (misc_model, lightmapped by q3map2): world.cmesh as OBJ, one material per (atlas tile,
  quantized vertex tint) -> generated shader: lightmap x tile x constant tint. The material mesh
  (materials.cmesh: kit pieces, curved patches, decals, emissives) as a second OBJ with the N10
  material definitions written back as Quake III shaders (tools/q3e/n10materials.py).
Sky: the map's N10 sky material (far box, cloud stages): the "$baked" far box of
  sky/reactor_bastion is baked from N10's sky geometry from the map centre (as the legacy game
  does), the sun direction taken from its glow; The Arsenal's nebula box is copied.
Entities: N10 lights -> light, spawns -> info_player_deathmatch (+ info_ca_enemy_spawn together
  with the pickup spots), jump pads -> trigger_push + target_position at the N10 apex (Quake III
  re-aims the launch for its own gravity).

Units: metres * 32, centred on the map centre. Usage:
  n10map2q3.py <repo root> <map id> <output name>     e.g. reactor-bastion ca_reactor
"""
import json
import math
import os
import struct
import sys

from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import n10materials  # noqa: E402

U = 32.0
CAULK = "common/caulk"
CLIP = "common/caulk"          # collision brushes are invisible; the visual mesh draws the world
PLAYERCLIP = "common/playerclip"
DETAIL = 134217728             # CONTENTS_DETAIL


def read_cmesh(path):
    d = open(path, "rb").read()
    assert d[:8] == b"CATMESH1", path
    stride, nv, ni = struct.unpack_from("<3I", d, 12)
    v = struct.unpack_from("<%df" % (nv * stride), d, 28)
    idx = struct.unpack_from("<%dI" % ni, d, 28 + nv * stride * 4)
    return stride, v, idx


class Writer:
    def __init__(self, cx, cy):
        self.cx, self.cy = cx, cy
        self.brushes = []

    def p(self, x, y, z):
        return ((x - self.cx) * U, (y - self.cy) * U, z * U)

    @staticmethod
    def face(p0, p1, p2, inside, tex, contents=0):
        # q3map2: normal = (p2 - p0) x (p1 - p0) points out of the brush
        a = [p2[i] - p0[i] for i in range(3)]
        b = [p1[i] - p0[i] for i in range(3)]
        n = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
        if sum(n[i] * (inside[i] - p0[i]) for i in range(3)) > 0:
            p1, p2 = p2, p1
        f = lambda q: "( %.3f %.3f %.3f )" % q
        return "%s %s %s %s 0 0 0 0.5 0.5 %d 0 0" % (f(p0), f(p1), f(p2), tex, contents)

    def prism(self, poly, top, bottom, tex=CLIP, contents=0, top_tex=None):
        """poly: convex [(x, y)] in metres; top / bottom: functions (x, y) -> z in metres."""
        pts = [(x, y) for x, y in poly]
        # drop repeated points
        clean = []
        for q in pts:
            if not clean or math.hypot(q[0] - clean[-1][0], q[1] - clean[-1][1]) > 1e-4:
                clean.append(q)
        if len(clean) > 2 and math.hypot(clean[0][0] - clean[-1][0], clean[0][1] - clean[-1][1]) < 1e-4:
            clean.pop()
        if len(clean) < 3:
            return False
        area = sum(clean[i][0] * clean[(i + 1) % len(clean)][1] - clean[(i + 1) % len(clean)][0] * clean[i][1] for i in range(len(clean)))
        if abs(area) < 1e-4:
            return False
        cxm = sum(q[0] for q in clean) / len(clean)
        cym = sum(q[1] for q in clean) / len(clean)
        zt, zb = top(cxm, cym), bottom(cxm, cym)
        if zt - zb < 0.01:
            return False
        inside = self.p(cxm, cym, (zt + zb) / 2)
        faces = []
        for i in range(len(clean)):
            a, b = clean[i], clean[(i + 1) % len(clean)]
            faces.append(self.face(self.p(a[0], a[1], zb), self.p(b[0], b[1], zb), self.p(a[0], a[1], zt), inside, tex, contents))
        # top / bottom through three well-separated vertices
        n = len(clean)
        i0, i1, i2 = 0, n // 3, (2 * n) // 3
        faces.append(self.face(*[self.p(clean[i][0], clean[i][1], top(*clean[i])) for i in (i0, i1, i2)], inside, top_tex or tex, contents))
        faces.append(self.face(*[self.p(clean[i][0], clean[i][1], bottom(*clean[i])) for i in (i0, i1, i2)], inside, tex, contents))
        self.brushes.append("{\n" + "\n".join(faces) + "\n}\n")
        return True


def slab_top(s):
    pl = s.get("plane")
    if not pl:
        return lambda x, y: s["z"]
    def f(x, y):
        u = ((x - pl["ox"]) * pl["dx"] + (y - pl["oy"]) * pl["dy"]) / pl["len"]
        u = min(1.0, max(0.0, u))
        return pl["z0"] + (pl["z1"] - pl["z0"]) * u
    return f


def unclamped_plane(s):
    pl = s["plane"]
    return lambda x, y: pl["z0"] + (pl["z1"] - pl["z0"]) * (((x - pl["ox"]) * pl["dx"] + (y - pl["oy"]) * pl["dy"]) / pl["len"])


def main():
    root, map_id, name = sys.argv[1], sys.argv[2], sys.argv[3]
    src = os.path.join(root, "assets", "n1", "maps", map_id)
    out = os.path.join(root, "game", "base", "catarena")
    doc = json.load(open(os.path.join(src, "map.json")))
    c = doc["compiled"]
    cx, cy = c["meta"]["center"]
    w = Writer(cx, cy)
    stats = {"slabs": 0, "solids": 0, "walls": 0, "skipped": 0}

    for s in c["slabs"]:
        top = slab_top(s) if not s.get("plane") else unclamped_plane(s)
        if s.get("base") is not None:
            base = s["base"]
            bottom = lambda x, y, b=base: b
        elif s.get("plane"):
            th = s["thickness"]
            bottom = lambda x, y, t=top, th=th: t(x, y) - th
        else:
            bottom = lambda x, y, z=s["z"] - s["thickness"]: z
        ok = w.prism(s["poly"], top, bottom)
        stats["slabs" if ok else "skipped"] += 1
    for so in c["solids"]:
        ok = w.prism(so["poly"], lambda x, y, z=so["z1"]: z, lambda x, y, z=so["z0"]: z)
        stats["solids" if ok else "skipped"] += 1
    half = 0.08
    for wl in c["walls"]:
        ax, ay, bx, by = wl["ax"], wl["ay"], wl["bx"], wl["by"]
        L = math.hypot(bx - ax, by - ay)
        if L < 0.02 or wl["z1"] - wl["z0"] < 0.02:
            stats["skipped"] += 1
            continue
        nx, ny = -(by - ay) / L * half, (bx - ax) / L * half
        poly = [(ax + nx, ay + ny), (bx + nx, by + ny), (bx - nx, by - ny), (ax - nx, ay - ny)]
        ok = w.prism(poly, lambda x, y, z=wl["z1"]: z, lambda x, y, z=wl["z0"]: z, contents=DETAIL)
        stats["walls" if ok else "skipped"] += 1

    # footprint edges: where the outside of a footprint polygon's edge is in no other polygon, a
    # player clip wall 0.12 m thick from the lowest to the highest point of the map
    def in_poly(x, y, poly):
        inside = False
        n = len(poly)
        for i in range(n):
            (x0, y0), (x1, y1) = poly[i], poly[(i + 1) % n]
            if (y0 > y) != (y1 > y) and x < x0 + (y - y0) * (x1 - x0) / (y1 - y0):
                inside = not inside
        return inside
    foot = c["footprint"]
    def walkable(x, y):
        return any(in_poly(x, y, q) for q in foot)
    zlow_e = min(s["z"] - s["thickness"] if s.get("base") is None else s["base"] for s in c["slabs"]) - 2
    zhigh_e = max([wl["z1"] for wl in c["walls"]] + [so["z1"] for so in c["solids"]]) + 12
    stats["edge clips"] = 0
    for poly in foot:
        n = len(poly)
        area = sum(poly[i][0] * poly[(i + 1) % n][1] - poly[(i + 1) % n][0] * poly[i][1] for i in range(n))
        sign = 1 if area > 0 else -1          # counter-clockwise: outward normal (dy, -dx)
        for i in range(n):
            (ax, ay), (bx, by) = poly[i], poly[(i + 1) % n]
            L = math.hypot(bx - ax, by - ay)
            if L < 0.05:
                continue
            ox, oy = sign * (by - ay) / L, -sign * (bx - ax) / L
            steps = max(1, int(math.ceil(L / 0.25)))
            run = None
            for k in range(steps + 1):
                t = (k + 0.5) / steps if k < steps else None
                open_edge = False
                if t is not None:
                    mx, my = ax + (bx - ax) * t, ay + (by - ay) * t
                    open_edge = not walkable(mx + ox * 0.06, my + oy * 0.06)
                if open_edge and run is None:
                    run = k / steps
                if (not open_edge or t is None) and run is not None:
                    t0, t1 = run, k / steps
                    run = None
                    p0 = (ax + (bx - ax) * t0, ay + (by - ay) * t0)
                    p1 = (ax + (bx - ax) * t1, ay + (by - ay) * t1)
                    quad = [p0, p1, (p1[0] + ox * 0.12, p1[1] + oy * 0.12), (p0[0] + ox * 0.12, p0[1] + oy * 0.12)]
                    if w.prism(quad, lambda x, y: zhigh_e, lambda x, y: zlow_e, tex=PLAYERCLIP, contents=DETAIL):
                        stats["edge clips"] += 1

    # hull: sky on top and around, caulk underneath
    xs = [p[0] for poly in c["footprint"] for p in poly]
    ys = [p[1] for poly in c["footprint"] for p in poly]
    x0, x1, y0, y1 = min(xs) - 3, max(xs) + 3, min(ys) - 3, max(ys) + 3
    zlow = min(s["z"] - s["thickness"] if s.get("base") is None else s["base"] for s in c["slabs"]) - 3
    zhigh = max([wl["z1"] for wl in c["walls"]] + [so["z1"] for so in c["solids"]]) + 14
    T = 1.0
    sky = "catarena/sky_" + name
    def box(ax0, ay0, az0, ax1, ay1, az1, tex):
        w.prism([(ax0, ay0), (ax1, ay0), (ax1, ay1), (ax0, ay1)], lambda x, y: az1, lambda x, y: az0, tex=tex)
    box(x0 - T, y0 - T, zlow - T, x1 + T, y1 + T, zlow, CAULK)
    box(x0 - T, y0 - T, zhigh, x1 + T, y1 + T, zhigh + T, sky)
    box(x0 - T, y0 - T, zlow, x0, y1 + T, zhigh, sky)
    box(x1, y0 - T, zlow, x1 + T, y1 + T, zhigh, sky)
    box(x0, y0 - T, zlow, x1, y0, zhigh, sky)
    box(x0, y1, zlow, x1, y1 + T, zhigh, sky)

    # ---- visual mesh -> OBJ + shaders ---------------------------------------------------------
    stride, v, idx = read_cmesh(os.path.join(src, "world.cmesh"))
    nv = len(v) // stride
    key = []
    for i in range(nv):
        layer = int(round(v[i * stride + 11]))
        tint = tuple(int(round(v[i * stride + 6 + k] * 8)) for k in range(3))
        key.append((layer, tint))
    groups = {}
    for t in range(0, len(idx), 3):
        k = key[idx[t]]
        groups.setdefault(k, []).append(idx[t:t + 3])
    obj_rel = "maps/%s_world.obj" % name
    mtl_rel = "maps/%s_world.mtl" % name
    shader_lines = ["// %s: generated by tools/q3e/n10map2q3.py from N10 %s (world atlas tile x tint, q3map2 lightmap)\n" % (name, map_id)]
    os.makedirs(os.path.join(out, "maps"), exist_ok=True)
    with open(os.path.join(out, obj_rel), "w", newline="\n") as f, open(os.path.join(out, mtl_rel), "w", newline="\n") as m:
        f.write("# N10 %s world mesh (%d triangles), Quake III units\nmtllib %s\no world\n" % (map_id, len(idx) // 3, os.path.basename(mtl_rel)))
        for i in range(nv):
            x, y, z = w.p(v[i * stride], v[i * stride + 1], v[i * stride + 2])
            f.write("v %.3f %.3f %.3f\n" % (x, z, -y))   # q3map2 rotates assimp's Y-up: (x, y, z) -> (x, -z, y)
        for i in range(nv):
            f.write("vt %.5f %.5f\n" % (v[i * stride + 9], v[i * stride + 10]))   # assimp flips V; N10 tiles are row-flipped
        for i in range(nv):
            f.write("vn %.4f %.4f %.4f\n" % (v[i * stride + 3], v[i * stride + 5], -v[i * stride + 4]))
        for (layer, tint), tris in sorted(groups.items()):
            shader = "textures/catarena/%s/l%02d_%d%d%d" % (name, layer, *tint)
            m.write("newmtl %s\n" % shader)
            f.write("usemtl %s\n" % shader)
            for a, b, cc in tris:
                f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a + 1, a + 1, a + 1, b + 1, b + 1, b + 1, cc + 1, cc + 1, cc + 1))
            r, g, bl = (min(1.0, t / 8.0) for t in tint)
            shader_lines.append("""%s
{
	qer_editorimage textures/catarena/world/layer%02d.png
	{
		map $lightmap
		rgbGen identity
	}
	{
		map textures/catarena/world/layer%02d.png
		blendFunc GL_DST_COLOR GL_ZERO
		rgbGen const ( %.3f %.3f %.3f )
	}
}
""" % (shader, layer, layer, r * 1.6 if r * 1.6 < 1 else 1.0, g * 1.6 if g * 1.6 < 1 else 1.0, bl * 1.6 if bl * 1.6 < 1 else 1.0))
    # sky: the map's N10 sky material; a "$baked" far box is baked from the N10 sky geometry as the
    # legacy game does (bakeSkyBox), a named one copied from the N10 catalog
    meta_sky = doc["authored"]["meta"].get("sky") or {}
    sky_name = meta_sky.get("shader", "sky/reactor_bastion")
    sd = {d["name"]: d for d in doc["materialDefinitions"]}[sky_name]
    sky_textures = set()
    stages = ""
    for st in sd["stages"]:
        st = dict(st)
        if (st.get("rgbGen") or {}).get("type") == "vertex":
            # N10 tints the cloud dome by the sun (legacy sky_dome vertex colours); Quake III's cloud
            # layers have no such colours: the mean of N10's tint
            st["rgbGen"] = {"type": "const", "color": [0.83, 0.51, 0.36]}
            st["alphaGen"] = {"type": "identity"}
        text, tex = n10materials.stage_text(st, set())
        stages += text or ""
        sky_textures.update(tex)
    far = sd["sky"].get("farbox") or "-"
    az, el = meta_sky.get("sunAzimuth", -0.62), meta_sky.get("sunElevation", 0.1)
    if far == "$baked":
        far = "textures/catarena/sky/%s_box" % name
        sun = n10materials.bake_sky_box(os.path.join(src, "sky.cmesh"), (cx, cy), os.path.join(out, "textures", "catarena", "sky"), "%s_box" % name)
        az, el = math.atan2(sun[1], sun[0]), math.asin(max(-1.0, min(1.0, sun[2])))
        sun_color = [1.0, 0.66, 0.45]
    else:
        if far != "-":
            sky_textures.update(far + "_" + side for side in ("rt", "lf", "bk", "ft", "up", "dn"))
        sun_color = meta_sky.get("sunColor", [1, 0.94, 0.82])
    amb = meta_sky.get("openAmbient", [0.5, 0.45, 0.42])
    shader_lines.append("""textures/%s
{
	surfaceparm sky
	surfaceparm noimpact
	surfaceparm nolightmap
	surfaceparm nomarks
	q3map_sunExt %.3f %.3f %.3f 140 %.1f %.1f 2 16
	q3map_skylight %d 3
	skyparms %s %d -
%s}
""" % (sky, sun_color[0], sun_color[1], sun_color[2], math.degrees(az), max(10.0, math.degrees(el)),
       int(60 + 40 * sum(amb) / 3), far, int(sd["sky"].get("cloudHeight") or 128), stages))

    # ---- material mesh (kit pieces, patches, emissives) -> second OBJ + shaders ---------------------
    mat_shaders, mat_textures, mat_layers, portals, mat_tris = n10materials.write_materials(root, src, out, name, doc, w)
    shader_lines += mat_shaders
    # MD3 props (The Arsenal: the waving flag, turrets): the N10 models and their shaders; cgame
    # animates them (misc_ca_prop entities)
    props = doc["authored"].get("models") or []
    if props:
        defs = {d["name"]: d for d in doc["materialDefinitions"]}
        rigs = {"flag": ["flag"], "turret": ["turret_base", "turret_head", "turret_barrel"]}
        files = sorted({f for pr in props for f in rigs[pr["model"]]})
        md3_shaders = set()
        os.makedirs(os.path.join(out, "models", "gallery"), exist_ok=True)
        for f in files:
            data = open(os.path.join(root, "assets", "n1", "models", "md3", f + ".md3"), "rb").read()
            with open(os.path.join(out, "models", "gallery", f + ".md3"), "wb") as o:
                o.write(data)
            nsurf, osurf = struct.unpack_from("<i", data, 84)[0], struct.unpack_from("<i", data, 100)[0]
            for _ in range(nsurf):
                sh_ofs = struct.unpack_from("<i", data, osurf + 92)[0]
                md3_shaders.add(data[osurf + sh_ofs:osurf + sh_ofs + 64].split(b"\0")[0].decode())
                osurf += struct.unpack_from("<i", data, osurf + 104)[0]
        for sh in sorted(md3_shaders):
            text, tex, _ = n10materials.shader_text(defs[sh], mat_layers)
            shader_lines.append(text)
            mat_textures |= set(tex)
    missing = n10materials.copy_textures(root, out, mat_textures | sky_textures)
    if missing:
        print("WARNING: textures not in the N10 catalog: %s" % ", ".join(missing))
    os.makedirs(os.path.join(out, "scripts"), exist_ok=True)
    with open(os.path.join(out, "scripts", name + ".shader"), "w", newline="\n") as f:
        f.write("\n".join(shader_lines))
    sl = os.path.join(out, "scripts", "shaderlist.txt")
    names = [l.strip() for l in open(sl)] if os.path.exists(sl) else []
    if name not in names:
        with open(sl, "a", newline="\n") as f:
            f.write(name + "\n")

    # textures: atlas tiles (N10 world_atlas_layers: 128 px tiles, tile m at (m % 4, m // 4), rows flipped)
    atlas = Image.open(os.path.join(root, "assets", "n2", "world-atlas-runtime.png")).convert("RGBA")
    os.makedirs(os.path.join(out, "textures", "catarena", "world"), exist_ok=True)
    for layer in sorted({k[0] for k in groups} | mat_layers):
        ox, oy = (layer % 4) * 128, (layer // 4) * 128
        atlas.crop((ox, oy, ox + 128, oy + 128)).convert("RGB").save(os.path.join(out, "textures", "catarena", "world", "layer%02d.png" % layer))
    os.makedirs(os.path.join(out, "textures", "catarena", "sky"), exist_ok=True)

    # N10 compiled map data for the server's navigation graph (N10 NavGraph, reused in qagame)
    import shutil
    shutil.copyfile(os.path.join(src, "map.cadata"), os.path.join(out, "maps", name + ".cadata"))

    # ---- entities -------------------------------------------------------------------------------
    ents = []
    world = ('{\n"classname" "worldspawn"\n"message" "%s (Cat Arena, converted from N10 %s)"\n"_ambient" "6"\n"_color" "1 0.96 0.9"\n'
             % (doc.get("level", {}).get("name", map_id) if isinstance(doc.get("level"), dict) else map_id, map_id)) + "".join(w.brushes) + "}\n"
    ents.append('{\n"classname" "misc_model"\n"model" "%s"\n"spawnflags" "4"\n"origin" "0 0 0"\n}\n' % obj_rel)
    if mat_tris:
        ents.append('{\n"classname" "misc_model"\n"model" "maps/%s_materials.obj"\n"spawnflags" "4"\n"origin" "0 0 0"\n}\n' % name)
    for pr in doc["authored"].get("models") or []:
        x, y, z = w.p(pr["x"], pr["y"], pr["z"])
        ents.append('{\n"classname" "misc_ca_prop"\n"model" "%s"\n"origin" "%.1f %.1f %.1f"\n"angle" "%.2f"\n"modelscale" "%g"\n}\n'
                    % (pr["model"], x, y, z, math.degrees(pr["yaw"]), pr.get("scale", 1)))
    # portal surfaces (N10 draws them as stand-ins; Quake III renders them): a misc_portal_surface at
    # the centre of each surface, with a misc_portal_camera at the N10 camera for windows / feeds
    for k, pt in enumerate(portals):
        o = pt["origin"]
        poly = o.get("poly3") or []
        if not poly:
            continue
        mx = sum(q[0] for q in poly) / len(poly)
        my = sum(q[1] for q in poly) / len(poly)
        mz = sum(q[2] for q in poly) / len(poly)
        x, y, z = w.p(mx, my, mz)
        cam = (o.get("portal") or {}).get("camera")
        if cam:
            cname = "%s_portalcam%d" % (name, k)
            ents.append('{\n"classname" "misc_portal_surface"\n"origin" "%.1f %.1f %.1f"\n"target" "%s"\n}\n' % (x, y, z, cname))
            cxq, cyq, czq = w.p(cam["x"], cam["y"], cam["z"])
            ents.append('{\n"classname" "misc_portal_camera"\n"spawnflags" "4"\n"targetname" "%s"\n"origin" "%.1f %.1f %.1f"\n"angles" "%.1f %.1f 0"\n}\n'
                        % (cname, cxq, cyq, czq, math.degrees(cam.get("pitch", 0)), math.degrees(cam.get("yaw", 0))))
        else:   # mirror
            ents.append('{\n"classname" "misc_portal_surface"\n"origin" "%.1f %.1f %.1f"\n}\n' % (x, y, z))
    for s in c["spawns"]:
        x, y, z = w.p(s["x"], s["y"], s["z"])
        yaw = math.degrees(s["a"])   # N10 player facing: forward = (cos a, sin a)
        ents.append('{\n"classname" "info_player_deathmatch"\n"origin" "%.1f %.1f %.1f"\n"angle" "%.1f"\n}\n' % (x, y, z + 26, yaw))
        ents.append('{\n"classname" "info_ca_enemy_spawn"\n"origin" "%.1f %.1f %.1f"\n}\n' % (x, y, z + 26))
    for pk in c["pickups"]:
        x, y, z = w.p(pk["x"], pk["y"], pk["z"])
        ents.append('{\n"classname" "info_ca_enemy_spawn"\n"origin" "%.1f %.1f %.1f"\n}\n' % (x, y, z + 26))
    for i, pd in enumerate(c["pads"]):
        tname = "%s_pad%d" % (name, i)
        px, py, pz = pd["x"], pd["y"], pd["z"]
        r = pd["r"]
        trig = Writer(cx, cy)
        trig.prism([(px - r, py - r), (px + r, py - r), (px + r, py + r), (px - r, py + r)], lambda x, y: pz + 0.5, lambda x, y: pz, tex="common/trigger")
        ents.append('{\n"classname" "trigger_push"\n"target" "%s"\n%s}\n' % (tname, "".join(trig.brushes)))
        ax, ay, az = w.p(*pd["apex"])
        ents.append('{\n"classname" "target_position"\n"targetname" "%s"\n"origin" "%.1f %.1f %.1f"\n}\n' % (tname, ax, ay, az))
    for li in c["lights"]:
        x, y, z = w.p(li["x"], li["y"], li["z"])
        col = li.get("color")
        if isinstance(col, str) and col.startswith("#") and len(col) == 7:
            rgb = tuple(int(col[i:i + 2], 16) / 255 for i in (1, 3, 5))
        elif isinstance(col, (int, float)):
            n = int(col)
            rgb = ((n >> 16 & 255) / 255, (n >> 8 & 255) / 255, (n & 255) / 255)
        else:
            rgb = (1.0, 0.86, 0.66) if li.get("warm") else (0.78, 0.88, 1.0)
        intensity = li.get("range", 12) * U * 0.55 * min(2.0, li.get("power", 1.0))
        ents.append('{\n"classname" "light"\n"origin" "%.1f %.1f %.1f"\n"light" "%.0f"\n"_color" "%.3f %.3f %.3f"\n}\n' % (x, y, z, intensity, *rgb))
    with open(os.path.join(out, "maps", name + ".map"), "w", newline="\n") as f:
        f.write("// %s: Cat Arena map converted from N10 %s by tools/q3e/n10map2q3.py (Quake III units, 32 u = 1 m)\n" % (name, map_id))
        f.write(world + "".join(ents))
    print("%s: %d slab, %d solid, %d wall brushes (%d skipped), %d footprint edge clips, %d triangles in %d material groups, %d material-mesh triangles in %d materials, %d portal surfaces, %d lights, %d spawns, %d pads"
          % (name, stats["slabs"], stats["solids"], stats["walls"], stats["skipped"], stats["edge clips"], len(idx) // 3, len(groups), mat_tris, len(mat_shaders), len(portals), len(c["lights"]), len(c["spawns"]), len(c["pads"])))


if __name__ == "__main__":
    main()
