#!/usr/bin/env python3
"""N10-Q3E stage D part 5: the light scale that brings a compiled map's q3map2 lightmaps to the
brightness of N10's baked lightmap of the same map.

N10 draws texture x lightmap x 1.9 (renderer.cpp, lightmap intensity 1, overbright 1); Quake III
stores lightmaps at half brightness and doubles them (mapOverBrightBits 2, r_overBrightBits 1). So
the target mean of the stored q3map2 luxels is 1.9 / 2 of N10's mean, both over lit luxels only.

Usage: light_scale.py <repo root> <N10 map id> <bsp>   prints the factor for q3map2 -light -scale
"""
import struct
import sys

import numpy as np
from PIL import Image

root, map_id, bsp = sys.argv[1], sys.argv[2], sys.argv[3]
data = open(bsp, "rb").read()
ofs, length = struct.unpack_from("<2i", data, 8 + 14 * 8)
q3 = np.frombuffer(data[ofs:ofs + length], dtype=np.uint8).reshape(-1, 3).astype(float)
q3 = q3[q3.sum(1) > 6]
n10 = np.asarray(Image.open("%s/assets/n1/lightmaps/%s.png" % (root, map_id)).convert("RGB")).reshape(-1, 3).astype(float)
n10 = n10[n10.sum(1) > 6]
target = n10.mean() * 1.9 / 2
print("%.3f" % max(1.0, min(6.0, target / max(1.0, q3.mean()))))
