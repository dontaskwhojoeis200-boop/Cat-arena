# N5.1: replays one recorded legacy tape offscreen, writes the frame after one tick and compares it
# with the picture the unmodified legacy game rendered at the same tick (tests/fixtures/n5/frames).
#   cmake -DAPP=<cat_arena> -DCOMPARE=<cat_arena_image_compare> -DFIXTURE=<helios_combat.bin> -DSCENARIO=<name> -DTICK=62
#         -DREFERENCE=<png> -DOUT=<dir> -DMIN_PSNR=35 -P effect_frame_check.cmake
file(MAKE_DIRECTORY "${OUT}")
# N6: -DKIND=enemy replays a tape of an enemy fixture (tests/fixtures/n6/<map>_enemies.bin, tools/n6/legacy_enemies.cjs --frames).
if(KIND STREQUAL "enemy")
  set(fixture_option --enemy-fixture)
else()
  set(fixture_option --combat-fixture)
endif()
execute_process(COMMAND "${APP}" --offscreen 1280x720 --no-hud ${fixture_option} "${FIXTURE}" --scenario ${SCENARIO} --capture-ticks ${TICK} --capture-dir "${OUT}"
                RESULT_VARIABLE run OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT run EQUAL 0)
  message(FATAL_ERROR "cat_arena failed (${run}):\n${log}")
endif()
string(LENGTH "${TICK}" digits)
set(padded "${TICK}")
while(digits LESS 5)
  set(padded "0${padded}")
  string(LENGTH "${padded}" digits)
endwhile()
execute_process(COMMAND "${COMPARE}" "${REFERENCE}" "${OUT}/${SCENARIO}_${padded}.png" ${MIN_PSNR} RESULT_VARIABLE cmp OUTPUT_VARIABLE out ERROR_VARIABLE out)
message("${out}")
if(NOT cmp EQUAL 0)
  message(FATAL_ERROR "frame ${SCENARIO} @ ${TICK} differs from the legacy reference")
endif()
