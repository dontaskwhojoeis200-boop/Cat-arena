#!/bin/sh
# N10-Q3E: compiles the Cat Arena maps in game/base/catarena/maps with q3map2 (BSP, VIS, LIGHT).
# Usage: tools/q3e/build_maps.sh <path to q3map2> [map names...]   (default: ca_test)
# q3map2 is built from NetRadiant-custom (GPL); see docs/q3e/BUILD_INSTRUCTIONS.md.
set -eu
Q3MAP2=$1; shift
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BASE=$ROOT/game/base
MAPS=${*:-ca_test}
COMMON="-game quake3 -fs_basepath $BASE -fs_basegame catarena -fs_homebase .catarena-q3map2"
for m in $MAPS; do
  MAP=$BASE/catarena/maps/$m.map
  case $m in ca_test) SS=8 ;; *) SS=${SAMPLESIZE:-16} ;; esac
  "$Q3MAP2" $COMMON -meta -samplesize $SS "$MAP"
  "$Q3MAP2" $COMMON -vis -saveprt "$MAP"
  "$Q3MAP2" $COMMON -light -fast -patchshadows -samples 2 -bounce 2 "$MAP"
  # N10 maps: light again with a scale until the lightmaps match the brightness of N10's baked
  # lightmap (within 15 %, at most three more passes; light does not respond linearly)
  N10=$(sed -n 's|^// [a-z_0-9]*: Cat Arena map converted from N10 \([a-z0-9-]*\) .*|\1|p' "$MAP" | head -n 1)
  if [ -n "$N10" ]; then
    SCALE=1
    for pass in 1 2 3; do
      RATIO=$(python3 "$ROOT/tools/q3e/light_scale.py" "$ROOT" "$N10" "$BASE/catarena/maps/$m.bsp")
      echo "light pass $pass for $m (N10 $N10): brightness ratio $RATIO, scale so far $SCALE"
      if python3 -c "import sys; sys.exit(0 if $RATIO <= 1.15 else 1)"; then break; fi
      SCALE=$(python3 -c "print('%.3f' % ($SCALE * $RATIO ** 1.4))")
      "$Q3MAP2" $COMMON -light -fast -patchshadows -samples 2 -bounce 2 -scale "$SCALE" "$MAP"
    done
    echo "light scale for $m: $SCALE"
  fi
  rm -f "$BASE/catarena/maps/$m.srf" "$BASE/catarena/maps/$m.prt"
  ls -l "$BASE/catarena/maps/$m.bsp"
done
