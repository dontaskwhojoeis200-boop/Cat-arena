# N6: renders one staged creature scene offscreen (cat_arena --enemy-lineup) and compares it with the picture
# the unmodified legacy game rendered of the same scene (tests/fixtures/n6/lineup, tools/n6/legacy_enemies.cjs --lineup-dir).
#   cmake -DAPP=<cat_arena> -DCOMPARE=<cat_arena_image_compare> -DLINEUP=<lineup.txt> -DSHOT=windup -DREFERENCE=<png> -DOUT=<dir> -DMIN_PSNR=45 -P lineup_check.cmake
file(MAKE_DIRECTORY "${OUT}")
execute_process(COMMAND "${APP}" --offscreen 1280x720 --no-hud --enemy-lineup "${LINEUP}" --lineup-shot ${SHOT} --capture "${OUT}/${SHOT}.png"
                RESULT_VARIABLE run OUTPUT_VARIABLE log ERROR_VARIABLE log)
if(NOT run EQUAL 0)
  message(FATAL_ERROR "cat_arena failed (${run}):\n${log}")
endif()
execute_process(COMMAND "${COMPARE}" "${REFERENCE}" "${OUT}/${SHOT}.png" ${MIN_PSNR} RESULT_VARIABLE cmp OUTPUT_VARIABLE out ERROR_VARIABLE out)
message("${out}")
if(NOT cmp EQUAL 0)
  message(FATAL_ERROR "staged scene ${SHOT} differs from the legacy reference")
endif()
