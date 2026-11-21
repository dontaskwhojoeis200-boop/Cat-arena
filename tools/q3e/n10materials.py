"""N10-Q3E stage D part 5: the N10 material mesh (materials.cmesh) and its materials in Quake III form.

N10's maps (from the legacy game) draw most of Helios Crucible, Bastion Keep and The Arsenal with
"materials": Quake III-style multi-stage shaders (map.json materialDefinitions; the legacy scripts
in assets/n1/materials/scripts) applied to the triangles of materials.cmesh (render.materials:
one index range per material). Quake III reads the same shader language, so each definition is
written back as a shader:

  stage maps     texture names from the N10 catalog, copied into the pk3 under the same name
                 ($world:N -> textures/catarena/world/layerNN.png, the N10 world atlas tile;
                 $lightmap, $whiteimage as in Quake III)
  $vertexlight   N10's kit pieces are "diffuse x vertex light", the vertex light being N10's
                 baked lightmap sampled at each vertex's lightmap coordinate x 1.9 (material.vert
                 vKit). The converter samples it the same way into the OBJ vertex colours; the
                 texture stage gets rgbGen vertex and q3map_noVertexLight keeps those colours
  blend, rgbGen, alphaGen, tcGen, tcMod, alphaFunc, depthWrite, depthFunc, clamp, detail,
  cull, sort, deformVertexes, polygonOffset, surfaceparms: written as Quake III keywords
  autosprite     deformVertexes autosprite / autosprite2 need each sprite as its own quad
                 surface, which a misc_model does not keep: those few sprites are drawn as fixed
                 quads (no deform)
  portals        $portal / $mirror / $portalfeed stages become Quake III portal surfaces
                 (shader keyword `portal`, with misc_portal_surface / misc_portal_camera entities)

Vertex layout of materials.cmesh (stride 18): position 3, normal 3, colour 4, uv 2, lightmap uv 2,
sprite 4.
"""
import json
import os
import struct

BLEND = ["GL_ZERO", "GL_ONE", "GL_SRC_COLOR", "GL_ONE_MINUS_SRC_COLOR", "GL_DST_COLOR", "GL_ONE_MINUS_DST_COLOR",
         "GL_SRC_ALPHA", "GL_ONE_MINUS_SRC_ALPHA", "GL_DST_ALPHA", "GL_ONE_MINUS_DST_ALPHA", "GL_SRC_ALPHA_SATURATE"]


def wave(w):
    return "%s %g %g %g %g" % (w["func"], w["base"], w["amp"], w["phase"], w["freq"])


def world_layer(name):
    return "textures/catarena/world/layer%02d.png" % int(name.split(":", 1)[1])


def stage_text(s, layers):
    """One stage; returns (text, texture names used). layers collects $world tiles."""
    lines, textures = [], []
    m = s.get("map") or ""
    if s.get("maps"):
        frames = []
        for f in s["maps"]:
            if f.startswith("$world:"):
                layers.add(int(f.split(":", 1)[1]))
                frames.append(world_layer(f))
            else:
                frames.append(f)
                textures.append(f)
        lines.append("animMap %g %s" % (s.get("animFreq") or 1, " ".join(frames)))
    elif m.startswith("$world:"):
        layers.add(int(m.split(":", 1)[1]))
        lines.append("%s %s" % ("clampmap" if s.get("clamp") else "map", world_layer(m)))
    elif m in ("$lightmap", "$whiteimage"):
        lines.append("map %s" % m)
    elif m in ("$portal", "$mirror", "$portalfeed"):
        return None, []      # the portal view itself: Quake III draws it before the surface's stages
    elif m:
        lines.append("%s %s" % ("clampmap" if s.get("clamp") else "map", m))
        textures.append(m)
    b = s.get("blend")
    if b:
        lines.append("blendFunc %s %s" % (BLEND[b[0]], BLEND[b[1]]))
    rgb = s.get("rgbGen") or {"type": "identity"}
    t = rgb["type"]
    if t == "const":
        lines.append("rgbGen const ( %g %g %g )" % tuple(rgb["color"]))
    elif t == "wave":
        lines.append("rgbGen wave %s" % wave(rgb["wave"]))
    elif t != "identity" or b:
        lines.append("rgbGen %s" % t)
    a = s.get("alphaGen") or {"type": "identity"}
    t = a["type"]
    if t == "wave":
        lines.append("alphaGen wave %s" % wave(a["wave"]))
    elif t == "const":
        lines.append("alphaGen const %g" % a.get("value", a.get("alpha", 1)))
    elif t == "portal":
        lines.append("alphaGen portal %g" % a.get("range", 256))
    elif t != "identity":
        lines.append("alphaGen %s" % t)
    tc = s.get("tcGen") or {"type": "base"}
    if tc["type"] == "lightmap" and m != "$lightmap":
        lines.append("tcGen lightmap")
    elif tc["type"] == "environment":
        lines.append("tcGen environment")
    elif tc["type"] == "vector":
        lines.append("tcGen vector ( %g %g %g ) ( %g %g %g )" % (*tc["s"], *tc["t"]))
    for mod in s.get("tcMods") or []:
        k = mod["type"]
        if k in ("scroll", "scale"):
            lines.append("tcMod %s %g %g" % (k, mod["s"], mod["t"]))
        elif k == "rotate":
            lines.append("tcMod rotate %g" % mod["speed"])
        elif k == "stretch":
            lines.append("tcMod stretch %s" % wave(mod["wave"]))
        elif k == "transform":
            lines.append("tcMod transform %g %g %g %g %g %g" % (*mod["m"], *mod["t"]))
        elif k == "turb":
            lines.append("tcMod turb %g %g %g %g" % (mod.get("base", 0), mod["amp"], mod["phase"], mod["freq"]))
    if s.get("alphaFunc"):
        lines.append("alphaFunc %s" % s["alphaFunc"])
    if s.get("depthWrite") and b:
        lines.append("depthWrite")
    if s.get("depthFunc") == "equal":
        lines.append("depthFunc equal")
    if s.get("detail"):
        lines.append("detail")
    return "\t{\n" + "".join("\t\t%s\n" % l for l in lines) + "\t}\n", textures


def shader_text(d, layers):
    """A Quake III shader from an N10 material definition. Returns (text, textures, kind) where kind
    is 'lightmap', 'vertex', 'portal' or 'plain'."""
    head, body, textures = [], [], []
    stages = list(d["stages"])
    vertex_lit = any((s.get("map") or "") == "$vertexlight" for s in stages)
    portal = d.get("portal") or d.get("mirror") or any((s.get("map") or "") in ("$portal", "$mirror", "$portalfeed") for s in stages)
    if d.get("cull") == "none" or d.get("autosprite"):
        head.append("cull none")
    elif d.get("cull") == "front":
        head.append("cull front")
    if portal:
        head.append("portal")
    if d.get("sort", 3) != 3 or portal:
        head.append("sort %d" % d.get("sort", 3))
    if d.get("polygonOffset"):
        head.append("polygonOffset")
    for p in d.get("surfaceparms") or []:
        head.append("surfaceparm %s" % p)
    for df in d.get("deforms") or []:
        k = df["type"]
        if k == "wave":
            head.append("deformVertexes wave %g %s" % (1.0 / df["spread"] if df["spread"] else 0, wave(df["wave"])))
        elif k == "bulge":
            head.append("deformVertexes bulge %g %g %g" % (df["width"], df["height"], df["speed"]))
        elif k == "move":
            head.append("deformVertexes move %g %g %g %s" % (*df["vec"], wave(df["wave"])))
        elif k == "normal":
            head.append("deformVertexes normal %g %g" % (df["amp"], df["freq"]))
        # autosprite / autosprite2: not kept (see the module notes)
    lightmapped = any((s.get("map") or "") == "$lightmap" for s in stages)
    if vertex_lit:
        head.append("surfaceparm nolightmap")
        head.append("q3map_noVertexLight")
    elif not lightmapped and "surfaceparm nolightmap" not in head:
        head.append("surfaceparm nolightmap")
    first_tex = True
    for s in stages:
        if (s.get("map") or "") == "$vertexlight":
            continue           # folded into rgbGen vertex of the texture stage
        if vertex_lit and first_tex and (s.get("map") or "") not in ("$lightmap",) and not s.get("blend"):
            s = dict(s)
            s["rgbGen"] = {"type": "vertex"}
            first_tex = False
        text, tex = stage_text(s, layers)
        if text:
            body.append(text)
            textures += tex
    kind = "portal" if portal else "vertex" if vertex_lit else "lightmap" if lightmapped else "plain"
    return "%s\n{\n%s%s}\n" % (d["name"], "".join("\t%s\n" % h for h in head), "".join(body)), textures, kind


def read_cmesh(path):
    data = open(path, "rb").read()
    assert data[:8] == b"CATMESH1", path
    stride, nv, ni = struct.unpack_from("<3I", data, 12)
    v = struct.unpack_from("<%df" % (nv * stride), data, 28)
    idx = struct.unpack_from("<%dI" % ni, data, 28 + nv * stride * 4)
    return stride, v, idx


def write_materials(root, src, out, name, doc, writer):
    """Writes maps/<name>_materials.obj / .mtl and returns (shader texts, texture names, $world
    layers, portal surfaces, triangle count). Nothing is written when the map has no material mesh."""
    groups = doc["render"]["materials"]
    if not groups:
        return [], set(), set(), [], 0
    defs = {d["name"]: d for d in doc["materialDefinitions"]}
    stride, v, idx = read_cmesh(os.path.join(src, "materials.cmesh"))
    assert stride == 18
    nv = len(v) // stride
    shaders, textures, layers, portals, done = [], set(), set(), [], set()
    obj = os.path.join(out, "maps", "%s_materials.obj" % name)
    mtl = os.path.join(out, "maps", "%s_materials.mtl" % name)
    by_material = {}
    for g in groups:
        by_material.setdefault(g["material"], []).append(g)
        o = g.get("origin") or {}
        if o.get("portal") or defs[g["material"]].get("mirror") or defs[g["material"]].get("portal"):
            portals.append({"material": g["material"], "origin": o})
    tris = 0
    # vertex light of the kit pieces: N10's baked lightmap at each vertex x 1.9 (material.vert vKit);
    # Quake III doubles map vertex colours for overbright, so half of it is stored
    import numpy as np
    from PIL import Image
    lm = np.asarray(Image.open(os.path.join(root, "assets", "n1", doc["lightmap"]["png"])).convert("RGB"), dtype=np.float64) / 255.0
    H, W = lm.shape[0], lm.shape[1]
    vertex_lit = set()
    for mat in by_material:
        if any((st.get("map") or "") == "$vertexlight" for st in defs[mat]["stages"]):
            for g in by_material[mat]:
                vertex_lit.update(idx[g["firstIndex"]:g["firstIndex"] + g["indexCount"]])
    def kit_light(i):
        u, t = v[i * stride + 12] * W - 0.5, v[i * stride + 13] * H - 0.5
        x0, y0 = int(np.floor(u)), int(np.floor(t))
        fx, fy = u - x0, t - y0
        def px(x, y):
            return lm[min(H - 1, max(0, y)), min(W - 1, max(0, x))]
        c = (px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx) * (1 - fy) + (px(x0, y0 + 1) * (1 - fx) + px(x0 + 1, y0 + 1) * fx) * fy
        return np.clip(c * 1.9 * 0.5, 0, 1)
    with open(obj, "w", newline="\n") as f, open(mtl, "w", newline="\n") as m:
        f.write("# N10 %s material mesh, Quake III units\nmtllib %s\no materials\n" % (doc.get("id", name), os.path.basename(mtl)))
        for i in range(nv):
            x, y, z = writer.p(v[i * stride], v[i * stride + 1], v[i * stride + 2])
            col = kit_light(i) if i in vertex_lit else (1.0, 1.0, 1.0)
            f.write("v %.3f %.3f %.3f %.3f %.3f %.3f\n" % (x, z, -y, col[0], col[1], col[2]))     # q3map2 rotates assimp's Y-up
        for i in range(nv):
            f.write("vt %.5f %.5f\n" % (v[i * stride + 10], 1.0 - v[i * stride + 11]))   # assimp flips V back
        for i in range(nv):
            f.write("vn %.4f %.4f %.4f\n" % (v[i * stride + 3], v[i * stride + 5], -v[i * stride + 4]))
        for mat, gs in by_material.items():
            if mat not in done:
                text, tex, kind = shader_text(defs[mat], layers)
                shaders.append(text)
                textures.update(tex)
                done.add(mat)
            m.write("newmtl %s\n" % mat)
            f.write("usemtl %s\n" % mat)
            for g in gs:
                first, count = g["firstIndex"], g["indexCount"]
                for t in range(first, first + count, 3):
                    a, b, c = idx[t] + 1, idx[t + 1] + 1, idx[t + 2] + 1
                    f.write("f %d/%d/%d %d/%d/%d %d/%d/%d\n" % (a, a, a, b, b, b, c, c, c))
                    tris += 1
    return shaders, textures, layers, portals, tris


def copy_textures(root, out, names):
    """Catalog texture names -> files in the pk3 tree under the same names (.rgba -> .png)."""
    from PIL import Image
    catalog = json.load(open(os.path.join(root, "assets", "n1", "catalog.json")))
    files = {}
    for t in catalog["textures"]:
        meta = t.get("metadata") or {}
        files[meta.get("name") or t["id"]] = (t["file"], meta.get("size"))
    missing = []
    for n in sorted(names):
        if n not in files:
            missing.append(n)
            continue
        rel, size = files[n]
        path = os.path.join(root, "assets", "n1", rel)
        dst_base = os.path.join(out, n)
        os.makedirs(os.path.dirname(dst_base), exist_ok=True)
        if rel.endswith(".rgba"):
            raw = open(path, "rb").read()
            side = int(round((len(raw) // 4) ** 0.5))
            Image.frombytes("RGBA", (side, side), raw).save(dst_base + ".png")
        else:
            ext = os.path.splitext(rel)[1]
            with open(path, "rb") as a, open(dst_base + ext, "wb") as b:
                b.write(a.read())
    return missing


def bake_sky_box(sky_cmesh, center, out_dir, base, size=512):
    """N10 / legacy bakeSkyBox: the sky geometry (vertex colours) seen from the map centre at z = 2 m
    into six faces, here as Quake III far box images <base>_rt/_lf/_bk/_ft/_up/_dn.jpg
    (rt +X, lf -X, bk +Y, ft -Y, up +Z, dn -Z; image left to right / top to bottom as Quake III's
    MakeSkyVec samples them). Ray casting against every triangle, nearest hit wins. Returns the
    direction of the brightest pixel (the sun glow of the N10 sky)."""
    import numpy as np
    from PIL import Image
    stride, v, idx = read_cmesh(sky_cmesh)
    V = np.array(v, dtype=np.float64).reshape(-1, stride)
    P, C = V[:, 0:3], V[:, 6:9]
    T = np.array(idx, dtype=np.int64).reshape(-1, 3)
    eye = np.array([center[0], center[1], 2.0])
    ii, jj = np.meshgrid(np.arange(size), np.arange(size))          # jj: row, ii: column
    s = (ii + 0.5) / size * 2 - 1
    t = 1 - (jj + 0.5) / size * 2
    one = np.ones_like(s)
    faces = {"rt": (one, -s, t), "lf": (-one, s, t), "bk": (s, one, t), "ft": (-s, -one, t),
             "up": (-t, -s, one), "dn": (t, -s, -one)}
    os.makedirs(out_dir, exist_ok=True)
    sun = [-1.0, 0.0, 0.0, 1.0]   # brightest sky direction (the sun glow): luminance, x, y, z
    for name, (dx, dy, dz) in faces.items():
        D = np.stack([dx, dy, dz], -1).reshape(-1, 3)
        D /= np.linalg.norm(D, axis=1, keepdims=True)
        best = np.full(len(D), np.inf)
        col = np.tile(np.array([0.10, 0.05, 0.04]), (len(D), 1))   # below the sky geometry
        for a, b, c in T:
            e1, e2 = P[b] - P[a], P[c] - P[a]
            h = np.cross(D, e2)
            det = h @ e1
            ok = np.abs(det) > 1e-9
            inv = np.where(ok, 1.0 / np.where(ok, det, 1), 0)
            sv = eye - P[a]
            u = (h @ sv) * inv
            q = np.cross(sv, e1)
            w = (D @ q) * inv
            dist = (q @ e2) * inv
            hit = ok & (u >= -1e-7) & (w >= -1e-7) & (u + w <= 1 + 1e-7) & (dist > 0) & (dist < best)
            if hit.any():
                best[hit] = dist[hit]
                col[hit] = (1 - u[hit] - w[hit])[:, None] * C[a] + u[hit][:, None] * C[b] + w[hit][:, None] * C[c]
        img = (np.clip(col, 0, 1) * 255 + 0.5).astype(np.uint8).reshape(size, size, 3)
        lum = col.sum(1)
        k = int(np.argmax(lum))
        if lum[k] > sun[0]:
            sun[:] = [lum[k], *D[k]]
        Image.fromarray(img, "RGB").save(os.path.join(out_dir, "%s_%s.jpg" % (base, name)), quality=92)
    return sun[1:]
