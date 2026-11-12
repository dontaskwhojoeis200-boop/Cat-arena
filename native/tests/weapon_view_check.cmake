# N5: renders one first-person weapon view offscreen and compares it with the picture of the same pose
# taken from the unmodified legacy game (tests/fixtures/n5/views, tools/n5/legacy_combat.cjs --views-dir).
# N6: --enemies none (the reference views have no enemies; waves are the default since N6).
#   cmake -DAPP=<cat_arena> -DCOMPARE=<cat_arena_image_compare> -DWEAPON=4 -DOTHER=9 -DREFERENCE=<png> -DOUT=<dir> -DMIN_PSNR=35 -P weapon_view_check.cmake
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${APP}" --offscreen 1280x720 --weapons ${WEAPON},${OTHER} --enemies none --no-hud --time 100 --player-pose 81.5,61.5,0,2.2252947962927703,0
                        --walk "-:1" --capture-every 1 --capture-dir "${OUT}" RESULT_VARIABLE run OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT run EQUAL 0)
  message(FATAL_ERROR "cat_arena failed (${run}):\n${log}")
endif()
execute_process(COMMAND "${COMPARE}" "${REFERENCE}" "${OUT}/walk_00000.png" ${MIN_PSNR} RESULT_VARIABLE cmp OUTPUT_VARIABLE out ERROR_VARIABLE out)
message("${out}")
if(NOT cmp EQUAL 0)
  message(FATAL_ERROR "weapon view ${WEAPON} differs from the legacy reference")
endif()
