#!/usr/bin/env python3
"""N7: writes assets/n7/fonts/FONTS_MANIFEST.json (file hashes, source package, licence) for the UI fonts.

The fonts are not legacy assets: the legacy page names Noto Serif / Georgia / Times New Roman, Arial and
Courier New / monospace and lets the browser pick an installed font. The native game ships the
metric-compatible Liberation fonts (SIL Open Font License 1.1) and DejaVu Sans Bold for the symbols
the Liberation fonts lack (Bitstream Vera licence; DejaVu changes public domain).

usage: python3 -I native/tools/n7/font_manifest.py assets/n7/fonts
"""
import hashlib
import json
import pathlib
import sys

FONTS = {
    "LiberationSerif-Bold.ttf": ("fonts-liberation 1:2.1.5-3 (Ubuntu 24.04)", "LICENSE-Liberation-OFL.txt", "serif: HUD numbers, stance, notices (legacy Noto Serif / Georgia / Times New Roman 900)"),
    "LiberationSerif-Regular.ttf": ("fonts-liberation 1:2.1.5-3 (Ubuntu 24.04)", "LICENSE-Liberation-OFL.txt", "serif_regular: menu buttons and titles (legacy serif 400)"),
    "LiberationSans-Bold.ttf": ("fonts-liberation 1:2.1.5-3 (Ubuntu 24.04)", "LICENSE-Liberation-OFL.txt", "sans: labels (legacy Arial 600 / 900)"),
    "LiberationMono-Bold.ttf": ("fonts-liberation 1:2.1.5-3 (Ubuntu 24.04)", "LICENSE-Liberation-OFL.txt", "mono: performance overlay, status lines (legacy monospace 600 / 800)"),
    "DejaVuSans-Bold.ttf": ("fonts-dejavu-core 2.37-8 (Ubuntu 24.04)", "LICENSE-DejaVu.txt", "fallback for symbols missing from Liberation (arrows, check marks, gear, ...)"),
}


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "assets/n7/fonts")
    files = []
    for name, (package, licence, use) in FONTS.items():
        data = (root / name).read_bytes()
        if not (root / licence).exists():
            raise SystemExit(f"missing licence {licence}")
        files.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "source": package, "licence": licence, "use": use})
    out = {"milestone": "N7", "kind": "third-party fonts (not legacy assets)", "files": files,
           "note": "Copied unmodified from the distribution packages named in 'source'. The legacy page relied on installed system fonts."}
    (root / "FONTS_MANIFEST.json").write_text(json.dumps(out, indent=1, ensure_ascii=False) + "\n", encoding="utf-8")
    print(f"wrote {root / 'FONTS_MANIFEST.json'} ({len(files)} fonts)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
