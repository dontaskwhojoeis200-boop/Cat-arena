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
  rm -f "$BASE/catarena/maps/$m.srf" "$BASE/catarena/maps/$m.prt"
  ls -l "$BASE/catarena/maps/$m.bsp"
done
