# Cat Arena: Feral Claw — game modules for the Quake3e engine (N10-Q3E).
#   qagame  server game: ioquake3 GPL game code (C) + Cat Arena C++20 code
#   cgame   client game: ioquake3 GPL cgame code (C) + Cat Arena C++20 code
#   ui      menus: Cat Arena C++20
# Built as native libraries named the way Quake3e loads them: <module>x86_64.dll / .so.
# GPL v2 (docs/q3e/LICENSING.md).
cmake_minimum_required(VERSION 3.20)
project(CatArenaGameModules C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(CA_ARCH x86_64)
else()
  message(FATAL_ERROR "Cat Arena modules are built for x86_64 only")
endif()

set(IOQ3 ${CMAKE_CURRENT_SOURCE_DIR}/ioq3/code)
set(CA   ${CMAKE_CURRENT_SOURCE_DIR}/catarena)

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  # The Quake III game code is old C: keep MSVC's warnings for it at the default level.
  set(CA_CXX_WARNINGS /W4 /permissive-)
else()
  add_compile_options($<$<COMPILE_LANGUAGE:C>:-fno-strict-aliasing>)
  set(CA_CXX_WARNINGS -Wall -Wextra -Wpedantic)
endif()

# Cat Arena C++20 code shared by every module (C bridge; gameplay code is added here in later stages).
add_library(ca_bridge STATIC ${CA}/src/ca_bridge.cpp ${CA}/src/ca_weapons.cpp ${CA}/src/ca_enemies.cpp)
target_include_directories(ca_bridge PUBLIC ${CA}/include)
# the legacy enemy tables generated for N10 (native/include/cat/generated/enemy_tables.inc)
target_include_directories(ca_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../native/include)
target_compile_options(ca_bridge PRIVATE ${CA_CXX_WARNINGS})

set(CA_SHARED_C ${IOQ3}/qcommon/q_math.c ${IOQ3}/qcommon/q_shared.c)

set(CA_BG_C ${IOQ3}/game/bg_misc.c ${IOQ3}/game/bg_pmove.c ${IOQ3}/game/bg_slidemove.c ${IOQ3}/game/bg_lib.c)

set(CA_GAME_C
  ${IOQ3}/game/g_main.c ${IOQ3}/game/ai_chat.c ${IOQ3}/game/ai_cmd.c ${IOQ3}/game/ai_dmnet.c
  ${IOQ3}/game/ai_dmq3.c ${IOQ3}/game/ai_main.c ${IOQ3}/game/ai_team.c ${IOQ3}/game/ai_vcmd.c
  ${IOQ3}/game/g_active.c ${IOQ3}/game/g_arenas.c ${IOQ3}/game/g_bot.c ${IOQ3}/game/g_client.c
  ${IOQ3}/game/g_cmds.c ${IOQ3}/game/g_combat.c ${IOQ3}/game/g_items.c ${IOQ3}/game/g_mem.c
  ${IOQ3}/game/g_misc.c ${IOQ3}/game/g_missile.c ${IOQ3}/game/g_mover.c ${IOQ3}/game/g_session.c
  ${IOQ3}/game/g_spawn.c ${IOQ3}/game/g_svcmds.c ${IOQ3}/game/g_target.c ${IOQ3}/game/g_team.c
  ${IOQ3}/game/g_trigger.c ${IOQ3}/game/g_utils.c ${IOQ3}/game/g_weapon.c ${IOQ3}/game/g_syscalls.c)

set(CA_CGAME_C
  ${IOQ3}/cgame/cg_main.c ${IOQ3}/cgame/cg_consolecmds.c ${IOQ3}/cgame/cg_draw.c
  ${IOQ3}/cgame/cg_drawtools.c ${IOQ3}/cgame/cg_effects.c ${IOQ3}/cgame/cg_ents.c
  ${IOQ3}/cgame/cg_event.c ${IOQ3}/cgame/cg_info.c ${IOQ3}/cgame/cg_localents.c
  ${IOQ3}/cgame/cg_marks.c ${IOQ3}/cgame/cg_particles.c ${IOQ3}/cgame/cg_players.c
  ${IOQ3}/cgame/cg_playerstate.c ${IOQ3}/cgame/cg_predict.c ${IOQ3}/cgame/cg_scoreboard.c
  ${IOQ3}/cgame/cg_servercmds.c ${IOQ3}/cgame/cg_snapshot.c ${IOQ3}/cgame/cg_view.c
  ${IOQ3}/cgame/cg_weapons.c ${IOQ3}/cgame/cg_syscalls.c ${IOQ3}/cgame/cg_ca_effects.c ${IOQ3}/cgame/cg_ca_sound.c)

function(ca_module target defs)
  set_target_properties(${target} PROPERTIES PREFIX "" OUTPUT_NAME ${target}${CA_ARCH})
  target_compile_definitions(${target} PRIVATE ${defs} CAT_ARENA_STANDALONE)
  target_include_directories(${target} PRIVATE ${CA}/include)
  target_link_libraries(${target} PRIVATE ca_bridge)
  if(NOT MSVC AND NOT APPLE)
    target_link_options(${target} PRIVATE -Wl,--no-undefined)
    target_link_libraries(${target} PRIVATE m)
  endif()
endfunction()

# N10 navigation reused unchanged (C++20): the navigation graph, the collision world it is built
# from, and N10's data reader for the compiled map data shipped in the pk3 (maps/<name>.cadata).
set(N10 ${CMAKE_CURRENT_SOURCE_DIR}/../native)
add_library(ca_n10nav STATIC ${N10}/src/navigation.cpp ${N10}/src/physics_world.cpp ${N10}/src/assets.cpp)
target_include_directories(ca_n10nav PUBLIC ${N10}/include)
# as in native/CMakeLists.txt: no floating-point contraction, so results match N10 on every platform
if(MSVC)
  target_compile_options(ca_n10nav PRIVATE /fp:precise)
else()
  target_compile_options(ca_n10nav PRIVATE -ffp-contract=off)
endif()

add_library(qagame SHARED ${CA_GAME_C} ${IOQ3}/game/g_ca.c ${IOQ3}/game/g_ca_weapons.c ${CA}/src/ca_horde.cpp ${CA_BG_C} ${CA_SHARED_C})
target_link_libraries(qagame PRIVATE ca_n10nav)
ca_module(qagame QAGAME)

add_library(cgame SHARED ${CA_CGAME_C} ${IOQ3}/cgame/cg_ca_enemies.c ${CA}/src/ca_enemy_anim.cpp ${CA_BG_C} ${CA_SHARED_C})
ca_module(cgame CGAME)
target_include_directories(cgame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../native/include)
set_source_files_properties(${CA}/src/ca_enemy_anim.cpp PROPERTIES COMPILE_OPTIONS "${CA_CXX_WARNINGS}")

add_library(ui SHARED ${CA}/ui/ui_main.cpp ${CA_SHARED_C})
ca_module(ui UI)
target_compile_options(ui PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CA_CXX_WARNINGS}>)
