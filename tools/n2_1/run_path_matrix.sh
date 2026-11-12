#!/bin/sh
# N2.1 evidence run: renders the seven Helios comparison poses (tools/n2/helios_poses.txt, 1280x720,
# time 10) on every Vulkan path and compares each PNG with the N2 golden frames
# (docs/n2-evidence/frames/native). Development/test tool; not needed to build or run the game.
#   run_path_matrix.sh <cat_arena> <project-root> <out-dir> [device-sim-layer-dir]
# The Vulkan driver / validation layer are taken from the caller's environment.
set -u
EXE=$1; ROOT=$2; OUT=$3; SIM=${4:-}
mkdir -p "$OUT"
run() {   # name, simulated api, hidden extensions, masked features, cat_arena options...
  name=$1; api=$2; hide=$3; nofeat=$4; shift 4
  mkdir -p "$OUT/$name"
  (
    if [ -n "$api$hide$nofeat" ]; then
      [ -n "$SIM" ] || exit 77
      if [ -n "${VK_LAYER_PATH:-}" ]; then export VK_LAYER_PATH="$SIM:$VK_LAYER_PATH"; else export VK_ADD_LAYER_PATH="$SIM"; fi
      export VK_INSTANCE_LAYERS=VK_LAYER_CAT_device_sim
      [ -n "$api" ] && export CAT_VKSIM_API="$api"
      [ -n "$hide" ] && export CAT_VKSIM_HIDE_EXT="$hide"
      [ -n "$nofeat" ] && export CAT_VKSIM_NO_FEATURES="$nofeat"
    fi
    "$EXE" --offscreen 1280x720 --capture-set "$ROOT/tools/n2/helios_poses.txt" --capture-dir "$OUT/$name" --time 10 \
           --report "$OUT/$name/report.json" "$@" > "$OUT/$name/run.log" 2>&1
  )
  echo "$name exit=$?"
}
run auto                 "" "" ""
run baseline             "" "" "" --vk-baseline
run cap12_extensions     "" "" "" --vk-max-api 1.2
run cap13_extensions     "" "" "" --vk-max-api 1.3
run cap13_core           "" "" "" --vk-max-api 1.3 --vk-no-extensions
run cap14_core           "" "" "" --vk-no-extensions
run no_dynamic_rendering "" "" "" --vk-disable dynamic_rendering
run no_synchronization2  "" "" "" --vk-disable synchronization2
run no_dynamic_state     "" "" "" --vk-disable extended_dynamic_state
run no_push_descriptor   "" "" "" --vk-disable push_descriptor
run no_host_image_copy   "" "" "" --vk-disable host_image_copy
if [ -n "$SIM" ]; then
  run sim_vk12_bare        1.2 "*" ""
  run sim_vk12_extensions  1.2 ""  ""
  run sim_vk12_dr_bit_off  1.2 ""  dynamicRendering
  run sim_vk13_bare        1.3 "*" ""
  run sim_vk13_no_bits     1.3 "*" dynamicRendering,synchronization2
  run sim_vk14_no_hostcopy 1.4 "*" hostImageCopy
  run sim_vk11_rejected    1.1 ""  ""
fi
