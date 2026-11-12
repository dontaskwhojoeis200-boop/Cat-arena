#!/usr/bin/env python3
"""Compare native Vulkan captures with legacy WebGL reference captures of the same poses.

    python3 tools/n2/compare_frames.py --native <dir> --legacy <dir> --out <dir>

Inputs: <name>.png in both directories (native from `cat_arena --capture-set`, legacy from
tools/n2/legacy_reference.cjs). Outputs <name>_compare.png (legacy | native | |difference|x4)
and comparison.json. Requires Pillow. Metrics are descriptive image statistics; they are
NOT a perceptual-parity verdict. Frames of different size are reported and skipped.
"""
import argparse, json, math, pathlib, sys
from PIL import Image, ImageChops, ImageDraw


def stats(a, b):
    w, h = a.size
    pa, pb = a.load(), b.load()
    n = w * h
    abs_sum = [0, 0, 0]
    sq = 0.0
    within8 = within24 = 0
    mean_a = [0, 0, 0]
    mean_b = [0, 0, 0]
    for y in range(h):
        for x in range(w):
            ca, cb = pa[x, y], pb[x, y]
            m = 0
            for c in range(3):
                d = abs(ca[c] - cb[c])
                abs_sum[c] += d
                sq += d * d
                m = max(m, d)
                mean_a[c] += ca[c]
                mean_b[c] += cb[c]
            within8 += m <= 8
            within24 += m <= 24
    mse = sq / (n * 3)
    return {
        "pixels": n,
        "meanAbsError": [round(s / n, 3) for s in abs_sum],
        "psnrDb": round(10 * math.log10(255 * 255 / mse), 3) if mse > 0 else None,
        "pixelsWithin8": round(within8 / n, 4),
        "pixelsWithin24": round(within24 / n, 4),
        "meanLegacyRGB": [round(s / n, 2) for s in mean_a],
        "meanNativeRGB": [round(s / n, 2) for s in mean_b],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--native", required=True)
    ap.add_argument("--legacy", required=True)
    ap.add_argument("--out", required=True)
    o = ap.parse_args()
    nat, leg, out = pathlib.Path(o.native), pathlib.Path(o.legacy), pathlib.Path(o.out)
    out.mkdir(parents=True, exist_ok=True)
    report = {"tool": "tools/n2/compare_frames.py", "note": "Descriptive pixel statistics only; not a perceptual parity verdict.", "frames": []}
    for lp in sorted(leg.glob("*.png")):
        np_ = nat / lp.name
        entry = {"name": lp.stem}
        if not np_.exists():
            entry["status"] = "MISSING_NATIVE"
            report["frames"].append(entry)
            continue
        a, b = Image.open(lp).convert("RGB"), Image.open(np_).convert("RGB")
        if a.size != b.size:
            entry.update(status="SIZE_MISMATCH", legacy=list(a.size), native=list(b.size))
            report["frames"].append(entry)
            continue
        entry.update(status="COMPARED", size=list(a.size), **stats(a, b))
        diff = ImageChops.difference(a, b).point(lambda v: min(255, v * 4))
        w, h = a.size
        sheet = Image.new("RGB", (w * 3, h + 24), (0, 0, 0))
        for i, (img, label) in enumerate(((a, "legacy WebGL2 (Chromium, lavapipe)"), (b, "native Vulkan (lavapipe)"), (diff, "|difference| x4"))):
            sheet.paste(img, (i * w, 24))
            ImageDraw.Draw(sheet).text((i * w + 6, 6), f"{lp.stem}: {label}", fill=(255, 255, 255))
        sheet.save(out / f"{lp.stem}_compare.png")
        report["frames"].append(entry)
        print(f"{lp.stem}: MAE {entry['meanAbsError']} PSNR {entry['psnrDb']} dB within8 {entry['pixelsWithin8']} within24 {entry['pixelsWithin24']}")
    (out / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
