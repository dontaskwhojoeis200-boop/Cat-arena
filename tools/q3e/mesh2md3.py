#!/usr/bin/env python3
"""N10-Q3E: converts N10 meshes (assets/n5/weapons/*.gun, assets/n6/enemies/*.emesh: triangle soups,
12 floats per vertex = position, normal, colour, uv, material id) into Quake III MD3 models.

N10 model space: Z up, facing +Y. Quake III model space: Z up, facing +X. So (x, y, z) ->
(y, -x, z) * scale. Vertices are de-duplicated; surfaces are split at 1000 vertices (the engine's
per-surface limit). One frame, no tags: animation is a later stage.

Usage: mesh2md3.py <input .gun|.emesh> <output .md3> <shader name> <scale> [z offset]
"""
import math
import struct
import sys

MAX_VERTS = 1000


def read_mesh(path):
    data = open(path, "rb").read()
    magic = data[:8]
    count = struct.unpack_from("<I", data, 8)[0]
    if magic == b"CATGUN01":
        off = 24
    elif magic == b"CATENM01":
        off = 80
    else:
        raise SystemExit("not an N10 mesh: " + path)
    floats = struct.unpack_from("<%df" % (count * 12), data, off)
    return [floats[i * 12:(i + 1) * 12] for i in range(count)]


def encode_normal(n):
    x, y, z = n
    length = math.sqrt(x * x + y * y + z * z) or 1.0
    x, y, z = x / length, y / length, z / length
    if x == 0 and y == 0:
        return 0 if z > 0 else 128
    lng = int(round(math.atan2(y, x) * 255 / (2 * math.pi))) & 255
    lat = int(round(math.acos(max(-1.0, min(1.0, z))) * 255 / (2 * math.pi))) & 255
    return (lat << 8) | lng


def build_surfaces(verts, scale, zoff):
    tris, cur_map, cur_verts, cur_tris = [], {}, [], []
    surfaces = []
    for t in range(0, len(verts), 3):
        idx = []
        tri = verts[t:t + 3]
        needed = sum(1 for v in tri if tuple(round(c, 5) for c in v[:11]) not in cur_map)
        if len(cur_verts) + needed > MAX_VERTS:
            surfaces.append((cur_verts, cur_tris))
            cur_map, cur_verts, cur_tris = {}, [], []
        for v in tri:
            key = tuple(round(c, 5) for c in v[:11])
            if key not in cur_map:
                x, y, z = v[0], v[1], v[2]
                nx, ny, nz = v[3], v[4], v[5]
                pos = (y * scale, -x * scale, z * scale + zoff)
                nrm = (ny, -nx, nz)
                cur_map[key] = len(cur_verts)
                cur_verts.append((pos, nrm, (v[9], v[10])))
            idx.append(cur_map[key])
        cur_tris.append((idx[0], idx[2], idx[1]))   # Quake III front faces wind the other way
    if cur_tris:
        surfaces.append((cur_verts, cur_tris))
    return surfaces


def name64(s):
    b = s.encode("ascii")[:63]
    return b + b"\0" * (64 - len(b))


def write_md3(path, model_name, shader, surfaces):
    allpos = [p for verts, _ in surfaces for p, _, _ in verts]
    mins = [min(p[i] for p in allpos) for i in range(3)]
    maxs = [max(p[i] for p in allpos) for i in range(3)]
    radius = max(math.sqrt(sum(c * c for c in p)) for p in allpos)
    frame = struct.pack("<3f3f3ff16s", *mins, *maxs, 0, 0, 0, radius, b"frame0")
    surf_blobs = []
    for i, (verts, tris) in enumerate(surfaces):
        hdr_size = 4 + 64 + 4 * 10
        tri_blob = b"".join(struct.pack("<3i", *t) for t in tris)
        shader_blob = name64(shader) + struct.pack("<i", 0)
        st_blob = b"".join(struct.pack("<2f", u, 1.0 - v) for _, _, (u, v) in verts)
        xyz_blob = b""
        for (x, y, z), n, _ in verts:
            q = [max(-32768, min(32767, int(round(c * 64)))) for c in (x, y, z)]
            xyz_blob += struct.pack("<3hH", *q, encode_normal(n))
        ofs_tri = hdr_size
        ofs_shader = ofs_tri + len(tri_blob)
        ofs_st = ofs_shader + len(shader_blob)
        ofs_xyz = ofs_st + len(st_blob)
        ofs_end = ofs_xyz + len(xyz_blob)
        hdr = struct.pack("<4s64s10i", b"IDP3", name64("surf%d" % i), 0, 1, 1, len(verts), len(tris),
                          ofs_tri, ofs_shader, ofs_st, ofs_xyz, ofs_end)
        surf_blobs.append(hdr + tri_blob + shader_blob + st_blob + xyz_blob)
    header_size = 4 + 4 + 64 + 4 * 9
    ofs_frames = header_size
    ofs_tags = ofs_frames + len(frame)
    ofs_surfaces = ofs_tags
    ofs_end = ofs_surfaces + sum(len(b) for b in surf_blobs)
    header = struct.pack("<4si64s9i", b"IDP3", 15, name64(model_name), 0, 1, 0, len(surf_blobs), 0,
                         ofs_frames, ofs_tags, ofs_surfaces, ofs_end)
    with open(path, "wb") as f:
        f.write(header + frame + b"".join(surf_blobs))
    return mins, maxs, sum(len(t) for _, t in surfaces), sum(len(v) for v, _ in surfaces)


def main():
    src, dst, shader, scale = sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4])
    zoff = float(sys.argv[5]) if len(sys.argv) > 5 else 0.0
    surfaces = build_surfaces(read_mesh(src), scale, zoff)
    model_name = dst.replace("\\", "/").split("catarena/", 1)[-1]
    mins, maxs, ntri, nvert = write_md3(dst, model_name, shader, surfaces)
    print("%s: %d triangles, %d vertices, %d surfaces, bounds %s .. %s" % (
        dst, ntri, nvert, len(surfaces), [round(c, 1) for c in mins], [round(c, 1) for c in maxs]))


if __name__ == "__main__":
    main()
