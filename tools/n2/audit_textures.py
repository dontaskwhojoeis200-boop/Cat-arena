#!/usr/bin/env python3
"""N2 texture audit, step 2: compare the legacy runtime GPU textures (legacy_texture_dump.cjs)
with the N1 bake, applying the legacy upload rules, and emit the runtime world atlas.

    python3 tools/n2/audit_textures.py --dump <dir> --assets assets/n1 --out assets/n2

Comparisons use all four channels explicitly (PIL getbbox() on RGBA only inspects alpha,
which hid real differences in a first ad-hoc check). Requires Pillow.
Writes <out>/world-atlas-runtime.png (canvas orientation, like N1 world-atlas.png) and
<out>/texture_audit.json.
"""
import argparse, hashlib, json, pathlib, sys
from PIL import Image


def channels_diff(a, b):
    """Return (mean abs error over RGBA, max abs error, fraction of differing pixels)."""
    pa, pb = a.tobytes(), b.tobytes()
    total = mx = 0
    differing = 0
    for i in range(0, len(pa), 4):
        d = [abs(pa[i + k] - pb[i + k]) for k in range(4)]
        s = sum(d)
        total += s
        mx = max(mx, max(d))
        differing += s > 0
    n = len(pa) // 4
    return round(total / (n * 4), 4), mx, round(differing / n, 4)


def raw_rgba(path):
    data = path.read_bytes()
    side = int(round((len(data) // 4) ** 0.5))
    return Image.frombytes("RGBA", (side, side), data)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True)
    ap.add_argument("--assets", required=True)
    ap.add_argument("--out", required=True)
    o = ap.parse_args()
    dump, assets, out = pathlib.Path(o.dump), pathlib.Path(o.assets), pathlib.Path(o.out)
    out.mkdir(parents=True, exist_ok=True)
    index = json.loads((dump / "texture_dump.json").read_text())
    catalog = json.loads((assets / "catalog.json").read_text())
    files = {}
    for t in catalog["textures"]:
        meta = t.get("metadata") or {}
        files[meta.get("name") or t["id"]] = t["file"]
    report = {"tool": "tools/n2/audit_textures.py", "legacySha256": index["sha256"], "renderer": index["renderer"], "map": index["map"],
              "rule": "GPU rows are in upload order; world layers are row-flipped tiles (rebuildWorld), Q3MAT arrays and the lightmap are unflipped",
              "world": [], "lightmap": None, "material": [], "kit": []}

    # world array: GPU layer m == flip(tile m of the canvas captured by rebuildWorld)
    gpu = Image.frombytes("RGBA", (512, 512), (dump / "world_array_gpu.rgba").read_bytes())
    n1 = Image.open(assets / files["$world"]).convert("RGBA")
    runtime = Image.new("RGBA", (512, 512))
    for m in range(16):
        box = ((m % 4) * 128, (m // 4) * 128, (m % 4) * 128 + 128, (m // 4) * 128 + 128)
        layer = gpu.crop(box)
        canvas_tile = layer.transpose(Image.FLIP_TOP_BOTTOM)
        runtime.paste(canvas_tile, box[:2])
        mae, mx, frac = channels_diff(canvas_tile, n1.crop(box))
        report["world"].append({"layer": m, "maeVsN1": mae, "maxVsN1": mx, "differingFraction": frac})
    runtime_path = out / "world-atlas-runtime.png"
    runtime.save(runtime_path)
    report["worldAtlasRuntime"] = {"file": runtime_path.name, "sha256": hashlib.sha256(runtime_path.read_bytes()).hexdigest()}

    lm_gpu = Image.frombytes("RGBA", (index["lightmap"]["w"], index["lightmap"]["h"]), (dump / "lightmap_gpu.rgba").read_bytes())
    lm_n1 = Image.open(assets / f"lightmaps/{index['map']}.png").convert("RGBA")
    mae, mx, frac = channels_diff(lm_gpu, lm_n1) if lm_gpu.size == lm_n1.size else (None, None, None)
    report["lightmap"] = {"size": list(lm_gpu.size), "hash": index["lightmap"]["hash"], "source": index["lightmap"]["source"], "maeVsN1": mae, "maxVsN1": mx, "differingFraction": frac}

    for group in ("material", "kit"):
        for e in index[group]:
            g = raw_rgba(dump / e["file"])
            name = e["name"]
            entry = {"name": name, "layer": e["layer"]}
            if name not in files:
                entry["status"] = "NOT_IN_N1_CATALOG"
            else:
                p = assets / files[name]
                ref = raw_rgba(p) if p.suffix == ".rgba" else Image.open(p).convert("RGBA")
                if ref.size != g.size:
                    entry.update(status="SIZE_MISMATCH", n1=list(ref.size), gpu=list(g.size))
                else:
                    mae, mx, frac = channels_diff(g, ref)
                    entry.update(status="COMPARED", file=files[name], maeVsN1=mae, maxVsN1=mx, differingFraction=frac,
                                 decoder="PIL" if p.suffix != ".rgba" else "raw")
            report[group].append(entry)
    (out / "texture_audit.json").write_text(json.dumps(report, indent=2) + "\n")
    w = report["world"]
    print("world layers differing from N1:", [x["layer"] for x in w if x["differingFraction"] > 0])
    print("lightmap vs N1:", report["lightmap"])
    for group in ("material", "kit"):
        rows = report[group]
        bad = [r for r in rows if r.get("status") != "COMPARED" or r["maxVsN1"] > 0]
        print(group, len(rows), "textures; not byte-identical:", [(r["name"], r.get("status"), r.get("maeVsN1"), r.get("maxVsN1")) for r in bad][:12])
    return 0


if __name__ == "__main__":
    sys.exit(main())
