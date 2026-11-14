cmake_minimum_required(VERSION 3.24)
# N6.5 Windows: link the MSVC C/C++ runtime statically (also into SDL3 and glslang when they are
# fetched), so a fresh Windows installation needs no Visual C++ Redistributable. Override on the
# command line (e.g. -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL) if a dynamic runtime is wanted.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
project(CatArena VERSION 0.0.1 LANGUAGES CXX)
option(CAT_BUILD_DESKTOP "Build SDL3/Vulkan triangle shell" ON)
option(CAT_FETCH_SDL "Fetch pinned SDL3 when package not available" OFF)
option(CAT_FETCH_GLSLANG "Build the pinned glslang (vulkan-sdk-1.4.309.0) when no glslc / glslangValidator is installed" OFF)
option(CAT_WINDOWS_GUI "Windows: build CatArena.exe for the GUI subsystem (no console window; log in CatArena.log)" ON)
set(CAT_MILESTONE "N10" CACHE STRING "Milestone name used in package names (CAT_ARENA_FERAL_CLAW_<milestone>_<platform>)")
set(CAT_GAME_DIR "." CACHE STRING "Directory below CMAKE_INSTALL_PREFIX that receives the game (executable, runtime libraries, assets)")
option(CAT_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
include(CTest)
add_library(cat_warnings INTERFACE)
if(MSVC)
  # /utf-8: sources are UTF-8. /bigobj: renderer.cpp and main.cpp exceed the default section count.
  target_compile_options(cat_warnings INTERFACE /W4 /permissive- /utf-8 /Zc:__cplusplus /bigobj)
  target_compile_definitions(cat_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  # Linux and Android give the main thread 8 MiB of stack, Windows 1 MiB by default: keep them equal.
  target_link_options(cat_warnings INTERFACE /STACK:8388608)
else()
  target_compile_options(cat_warnings INTERFACE -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror -Wno-missing-field-initializers)
  if(CAT_SANITIZERS)
    target_compile_options(cat_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(cat_warnings INTERFACE -fsanitize=address,undefined)
  endif()
endif()
add_library(cat_arena_core src/core.cpp)
target_include_directories(cat_arena_core PUBLIC include)
target_link_libraries(cat_arena_core PRIVATE cat_warnings)
add_executable(cat_arena_headless src/headless.cpp)
target_link_libraries(cat_arena_headless PRIVATE cat_arena_core cat_warnings)
if(BUILD_TESTING)
  add_executable(cat_arena_tests tests/core_tests.cpp)
  target_link_libraries(cat_arena_tests PRIVATE cat_arena_core cat_warnings)
  add_test(NAME core_foundation COMMAND cat_arena_tests)
  add_test(NAME headless_smoke COMMAND cat_arena_headless)
endif()
# N2.1 Vulkan capability model + render-path selection: no Vulkan headers, no GPU.
add_library(cat_arena_vkcaps src/vulkan_caps.cpp)
target_include_directories(cat_arena_vkcaps PUBLIC include)
target_link_libraries(cat_arena_vkcaps PRIVATE cat_warnings)
if(BUILD_TESTING)
  add_executable(cat_arena_vkcaps_tests tests/vkcaps_tests.cpp)
  target_link_libraries(cat_arena_vkcaps_tests PRIVATE cat_arena_vkcaps cat_warnings)
  add_test(NAME vulkan_path_selection COMMAND cat_arena_vkcaps_tests)
endif()
if(CAT_BUILD_DESKTOP)
  if(ANDROID)
    message(FATAL_ERROR "The Cat Arena release targets Windows desktop. Historical core sources can be built with CAT_BUILD_DESKTOP=OFF.")
  endif()
  # N2.1: the runtime baseline is Vulkan 1.1. The build needs only a Vulkan loader library of any
  # version (Vulkan 1.0 + WSI symbols are linked; everything newer is resolved at run time) and
  # compiles against the pinned C headers in third_party/vulkan-headers (1.4.309), so no particular
  # SDK version is required. CAT_VULKAN_HEADERS overrides the header directory.
  set(CAT_VULKAN_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/vulkan-headers/include" CACHE PATH "Vulkan C headers (>= 1.4.304)")
  if(NOT Vulkan_INCLUDE_DIR)   # FindVulkan insists on headers; only the loader library is taken from the system
    set(Vulkan_INCLUDE_DIR "${CAT_VULKAN_HEADERS}" CACHE PATH "Vulkan headers (pinned copy)")
  endif()
  if(WIN32 AND MSVC)
    # N6.5: no Vulkan SDK needed. The import library for vulkan-1.dll (the loader that every Vulkan
    # driver installs) is made from tests/vulkan_baseline_imports.txt, the same Vulkan 1.0 + WSI list the
    # link check enforces; lib.exe / llvm-lib build it at configure time. CAT_VULKAN_IMPORT_LIBRARY
    # overrides it (e.g. the SDK's vulkan-1.lib).
    set(CAT_VULKAN_IMPORT_LIBRARY "" CACHE FILEPATH "Windows: vulkan-1.lib to link (empty: generate one from the import allowlist)")
    if(CAT_VULKAN_IMPORT_LIBRARY)
      set(Vulkan_LIBRARIES "${CAT_VULKAN_IMPORT_LIBRARY}")
    else()
      include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/cat_vulkan_import_library.cmake")
      set(cat_lib_tool "${CMAKE_AR}")
      if(NOT cat_lib_tool)   # lib.exe sits next to link.exe
        get_filename_component(cat_linker_dir "${CMAKE_LINKER}" DIRECTORY)
        find_program(cat_lib_tool NAMES lib llvm-lib HINTS "${cat_linker_dir}" REQUIRED)
      endif()
      cat_vulkan_import_library("${CMAKE_CURRENT_SOURCE_DIR}/tests/vulkan_baseline_imports.txt" "${cat_lib_tool}" "${CMAKE_CURRENT_BINARY_DIR}/vulkan-import" cat_vk_lib)
      message(STATUS "Vulkan: import library for vulkan-1.dll generated (${cat_vk_lib_COUNT} Vulkan 1.0 + WSI commands); no Vulkan SDK used")
      set(Vulkan_LIBRARIES "${cat_vk_lib}")
    endif()
  else()
    find_package(Vulkan REQUIRED)
  endif()
  if(NOT EXISTS "${CAT_VULKAN_HEADERS}/vulkan/vulkan_core.h")
    message(FATAL_ERROR "CAT_VULKAN_HEADERS does not contain vulkan/vulkan_core.h: ${CAT_VULKAN_HEADERS}")
  endif()
  find_package(SDL3 3.2.10 CONFIG QUIET)
  if(NOT SDL3_FOUND AND CAT_FETCH_SDL)
    include(FetchContent)
    FetchContent_Declare(SDL3 URL https://codeload.github.com/libsdl-org/SDL/tar.gz/refs/tags/release-3.2.10 URL_HASH SHA256=8a6ddc3e70de897f7921da275ac71e375de89dad802aa2932649d1fea9068478)
    FetchContent_MakeAvailable(SDL3)
  elseif(NOT SDL3_FOUND)
    message(FATAL_ERROR "Install SDL3 3.2.10+ or set CAT_FETCH_SDL=ON")
  endif()
  find_program(CAT_GLSLC NAMES glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
  find_program(CAT_GLSLANG NAMES glslangValidator glslang HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
  if(NOT CAT_GLSLC AND NOT CAT_GLSLANG AND CAT_FETCH_GLSLANG)
    # N6.5: the shader compiler is built from the same Khronos release as the pinned headers
    # (glslang tag vulkan-sdk-1.4.309.0 = 15.2.0; the version used for every Linux build so far).
    include(FetchContent)
    set(ENABLE_OPT OFF CACHE BOOL "glslang: no SPIRV-Tools optimizer" FORCE)
    set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
    set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(ENABLE_GLSLANG_BINARIES ON CACHE BOOL "" FORCE)
    set(ENABLE_HLSL OFF CACHE BOOL "" FORCE)
    set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
    set(BUILD_EXTERNAL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(glslang GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git GIT_TAG 7200bc12a8979d13b22cd52de80ffb7d41939615 GIT_SHALLOW FALSE)
    FetchContent_MakeAvailable(glslang)
    set(CAT_GLSLANG glslang-standalone)
    message(STATUS "Shaders: compiled with glslang built from source (vulkan-sdk-1.4.309.0)")
  endif()
  if(NOT CAT_GLSLC AND NOT CAT_GLSLANG)
    message(FATAL_ERROR "Install glslc or glslangValidator (Vulkan SDK shader compiler), or configure with -DCAT_FETCH_GLSLANG=ON")
  endif()
  set(shader_includes "${CMAKE_CURRENT_SOURCE_DIR}/shaders/frame.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/material_data.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/overlay.glsl" "${CMAKE_CURRENT_SOURCE_DIR}/shaders/enemy.glsl")
  foreach(shader triangle.vert triangle.frag sky.vert sky.frag skybox.vert skybox.frag world.vert world.frag material.vert material.frag
          fx.vert fx.frag gun.vert gun.frag hud.vert hud.frag solid.vert dynamic.vert dynamic.frag enemy.vert enemy.frag ui.vert ui.frag)
    set(out "${CMAKE_CURRENT_BINARY_DIR}/shaders/${shader}.spv")
    if(CAT_GLSLC)
      set(compile_shader "${CAT_GLSLC}" --target-env=vulkan1.1)
    else()
      set(compile_shader "${CAT_GLSLANG}" -V --target-env vulkan1.1)
    endif()
    add_custom_command(OUTPUT "${out}"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/shaders"
      COMMAND ${compile_shader} "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}" -o "${out}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}" ${shader_includes} VERBATIM)
    list(APPEND shader_outputs "${out}")
  endforeach()
  add_custom_target(cat_shaders ALL DEPENDS ${shader_outputs})
  add_library(cat_arena_render STATIC src/renderer.cpp)
  target_include_directories(cat_arena_render PUBLIC include)
  target_include_directories(cat_arena_render SYSTEM PRIVATE "${CAT_VULKAN_HEADERS}")
  # Link the loader library only; Vulkan::Vulkan would also add the SDK's (possibly older) headers.
  target_link_libraries(cat_arena_render PUBLIC cat_arena_core cat_arena_scene cat_arena_vkcaps SDL3::SDL3 ${Vulkan_LIBRARIES} PRIVATE cat_warnings)
  add_dependencies(cat_arena_render cat_shaders)
  add_executable(cat_arena src/main.cpp src/audio_device.cpp src/platform_info.cpp src/desktop_display.cpp)
  # N7: menus, settings, the new HUD and sound (libraries defined further down)
  target_link_libraries(cat_arena PRIVATE cat_arena_render cat_arena_physics cat_arena_fx cat_arena_ui cat_arena_audio cat_arena_coop cat_warnings)
  if(WIN32)
    # N6.5: the Windows game is CatArena.exe. vulkan-1.dll is delay-loaded so that a PC without a Vulkan
    # driver gets an explanation (main.cpp checks LoadLibraryW before the first Vulkan call) instead of
    # the system's "DLL not found" box. SDL_main.h supplies WinMain with UTF-8 arguments.
    set_target_properties(cat_arena PROPERTIES OUTPUT_NAME CatArena)
    if(CAT_WINDOWS_GUI)
      set_target_properties(cat_arena PROPERTIES WIN32_EXECUTABLE ON)
    endif()
    if(MSVC)
      target_link_options(cat_arena PRIVATE /DELAYLOAD:vulkan-1.dll)
      target_link_libraries(cat_arena PRIVATE delayimp)
    endif()
  else()
    set_target_properties(cat_arena PROPERTIES INSTALL_RPATH "$ORIGIN")   # the packaged game finds libSDL3 next to it
  endif()
  # Development fallback only: a packaged build uses assets/n1 next to the executable or --assets.
  target_compile_definitions(cat_arena PRIVATE CAT_DEFAULT_ASSET_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
  add_custom_command(TARGET cat_arena POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:cat_arena>/assets/shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${shader_outputs} "$<TARGET_FILE_DIR:cat_arena>/assets/shaders" VERBATIM)
  if(WIN32 AND TARGET SDL3::SDL3-shared)
    add_custom_command(TARGET cat_arena POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:SDL3::SDL3-shared>" "$<TARGET_FILE_DIR:cat_arena>" VERBATIM)
  endif()
  # N6.5: `cmake --install <build> --prefix <dir>` makes a self-contained game folder: the executable,
  # the SDL3 runtime library, the compiled SPIR-V shaders, the baked assets, README.txt and the
  # third-party notices. tools/package/validate_package.cmake checks it, tools/package/zip_package.cmake zips it.
  install(TARGETS cat_arena RUNTIME DESTINATION "${CAT_GAME_DIR}")
  if(TARGET SDL3::SDL3-shared)
    if(WIN32)
      install(FILES "$<TARGET_FILE:SDL3::SDL3-shared>" DESTINATION "${CAT_GAME_DIR}")
    else()
      install(FILES "$<TARGET_FILE:SDL3::SDL3-shared>" DESTINATION "${CAT_GAME_DIR}" RENAME "$<TARGET_SONAME_FILE_NAME:SDL3::SDL3-shared>")
    endif()
  endif()
  install(FILES ${shader_outputs} DESTINATION "${CAT_GAME_DIR}/assets/shaders")
  foreach(cat_asset_set n1 n2 n4 n5 n6 n7)
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../assets/${cat_asset_set}/" DESTINATION "${CAT_GAME_DIR}/assets/${cat_asset_set}")
  endforeach()
  install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../README_WINDOWS.md" DESTINATION "${CAT_GAME_DIR}" RENAME README.txt)
  install(CODE "set(CAT_NOTICES \"${CMAKE_CURRENT_SOURCE_DIR}/third_party_notices\")
                set(CAT_OUT \"\${CMAKE_INSTALL_PREFIX}/${CAT_GAME_DIR}/THIRD_PARTY_LICENSES.txt\")" )
  install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/tools/package/third_party_licenses.cmake")
  if(BUILD_TESTING)
    add_test(NAME desktop_headless_smoke COMMAND cat_arena --headless-test)
    # GPU tests: offscreen Vulkan rendering of the real map. Exit 77 = no Vulkan device (SKIPPED, not PASSED).
    add_executable(cat_arena_render_tests tests/render_tests.cpp)
    target_link_libraries(cat_arena_render_tests PRIVATE cat_arena_render cat_warnings)
    add_test(NAME render_helios_offscreen COMMAND cat_arena_render_tests "${CMAKE_CURRENT_BINARY_DIR}/shaders" "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
    set_tests_properties(render_helios_offscreen PROPERTIES SKIP_RETURN_CODE 77 LABELS gpu)
    # N4: every map loads, renders offscreen and is validation-clean (exit 2 = validation errors).
    foreach(cat_map bastion-keep reactor-bastion q3-gallery)
      add_test(NAME render_map_${cat_map} COMMAND cat_arena --map ${cat_map} --offscreen 320x180 --time 10
        --capture "${CMAKE_CURRENT_BINARY_DIR}/render_map_${cat_map}.png" --shaders "${CMAKE_CURRENT_BINARY_DIR}/shaders" --assets "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
      set_tests_properties(render_map_${cat_map} PROPERTIES LABELS gpu)
    endforeach()
    # N2.1: every selectable Vulkan path renders the same frames (baseline 1.1 ... 1.4).
    # N5: weapons through the whole pipeline (model, effects, HUD) with the validation layer; then the
    # first-person view of four weapons against pictures of the legacy game.
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n5/fx-atlas.png")
      add_test(NAME render_weapons_overlay COMMAND cat_arena --offscreen 640x360 --enemies practice --weapons 4,1 --seed 3 --walk "fire:3,-:40,swap:2,-:30,fire:3,-:25,reload:2,-:20,w+fire@3:60"
        --capture-every 30 --capture-dir "${CMAKE_CURRENT_BINARY_DIR}/n5-overlay")
      add_test(NAME render_weapons_overlay_baseline COMMAND cat_arena --vk-baseline --offscreen 640x360 --enemies practice --weapons 10,6 --seed 3 --walk "fire:3,-:40,swap:2,-:30,fire:40,-:25"
        --capture-every 30 --capture-dir "${CMAKE_CURRENT_BINARY_DIR}/n5-overlay-baseline")
      add_executable(cat_arena_image_compare tests/image_compare.cpp)
      target_link_libraries(cat_arena_image_compare PRIVATE cat_arena_scene cat_warnings)
      foreach(cat_gun 00 04 07 10)
        math(EXPR cat_gun_index "${cat_gun}" OUTPUT_FORMAT DECIMAL)
        math(EXPR cat_gun_other "(${cat_gun_index} + 5) % 16")
        add_test(NAME render_weapon_view_${cat_gun} COMMAND ${CMAKE_COMMAND} "-DAPP=$<TARGET_FILE:cat_arena>" "-DCOMPARE=$<TARGET_FILE:cat_arena_image_compare>" -DWEAPON=${cat_gun_index} -DOTHER=${cat_gun_other}
          "-DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n5/views/view_${cat_gun}.png" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/n5-view-${cat_gun}" -DMIN_PSNR=40
          -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/weapon_view_check.cmake")
      endforeach()
    endif()
    # N5.1: frames during effects against pictures of the legacy game. They fail without the dynamic
    # lights (about 22-25 dB) and without the medkit billboard (32 dB); with them they are at 50-57 dB.
    if(TARGET cat_arena_image_compare)
      foreach(cat_frame "rocket_at_feet_moving:62" "rocket_and_grenade_jump:44" "health_pickup:5")
        string(REPLACE ":" ";" cat_frame_parts "${cat_frame}")
        list(GET cat_frame_parts 0 cat_scenario)
        list(GET cat_frame_parts 1 cat_tick)
        string(LENGTH "${cat_tick}" cat_digits)
        set(cat_padded "${cat_tick}")
        while(cat_digits LESS 5)
          set(cat_padded "0${cat_padded}")
          string(LENGTH "${cat_padded}" cat_digits)
        endwhile()
        add_test(NAME render_effect_frame_${cat_scenario} COMMAND ${CMAKE_COMMAND} "-DAPP=$<TARGET_FILE:cat_arena>" "-DCOMPARE=$<TARGET_FILE:cat_arena_image_compare>"
          "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n5/helios_combat.bin" -DSCENARIO=${cat_scenario} -DTICK=${cat_tick}
          "-DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n5/frames/${cat_scenario}_${cat_padded}.png" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/n5-frame-${cat_scenario}" -DMIN_PSNR=40
          -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/effect_frame_check.cmake")
      endforeach()
    endif()
    # N6: creatures. Staged scenes (all 20 types standing / winding up with projected shadows, the final
    # five with outlines, bodies in every death variant, two Marine Cats) and frames of recorded enemy
    # tapes, each against the picture the legacy game rendered of the same moment.
    if(TARGET cat_arena_image_compare AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/lineup/lineup.txt" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n6/enemy-assets.json")
      foreach(cat_shot idle windup strike_final5 bodies marines)
        add_test(NAME render_enemy_lineup_${cat_shot} COMMAND ${CMAKE_COMMAND} "-DAPP=$<TARGET_FILE:cat_arena>" "-DCOMPARE=$<TARGET_FILE:cat_arena_image_compare>"
          "-DLINEUP=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/lineup/lineup.txt" -DSHOT=${cat_shot}
          "-DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/lineup/${cat_shot}.png" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/n6-lineup" -DMIN_PSNR=45
          -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/lineup_check.cmake")
      endforeach()
      foreach(cat_frame "fight_spin:530" "fight_spin:1300" "flyers_wave5:2003" "wave_progression:2400" "stand_still:400")
        string(REPLACE ":" ";" cat_parts "${cat_frame}")
        list(GET cat_parts 0 cat_scenario)
        list(GET cat_parts 1 cat_tick)
        string(LENGTH "${cat_tick}" cat_digits)
        set(cat_padded "${cat_tick}")
        while(cat_digits LESS 5)
          set(cat_padded "0${cat_padded}")
          string(LENGTH "${cat_padded}" cat_digits)
        endwhile()
        add_test(NAME render_enemy_frame_${cat_scenario}_${cat_tick} COMMAND ${CMAKE_COMMAND} "-DAPP=$<TARGET_FILE:cat_arena>" "-DCOMPARE=$<TARGET_FILE:cat_arena_image_compare>" -DKIND=enemy
          "-DFIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/helios-crucible_enemies.bin" -DSCENARIO=${cat_scenario} -DTICK=${cat_tick}
          "-DREFERENCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/frames/${cat_scenario}_${cat_padded}.png" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/n6-frame-${cat_scenario}-${cat_tick}" -DMIN_PSNR=40
          -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/effect_frame_check.cmake")
      endforeach()
    endif()
    add_executable(cat_arena_path_tests tests/path_tests.cpp)
    target_link_libraries(cat_arena_path_tests PRIVATE cat_arena_render cat_warnings)
    add_test(NAME render_path_matrix COMMAND cat_arena_path_tests "${CMAKE_CURRENT_BINARY_DIR}/shaders" "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
    set_tests_properties(render_path_matrix PROPERTIES SKIP_RETURN_CODE 77 LABELS gpu TIMEOUT 1800)
    # N2.1: only Vulkan 1.0 + WSI commands may be linked (everything newer is resolved at run time).
    if(CMAKE_NM AND NOT MSVC)
      add_test(NAME vulkan_link_imports COMMAND ${CMAKE_COMMAND} "-DNM=${CMAKE_NM}" "-DBINARIES=$<TARGET_FILE:cat_arena_render>|$<TARGET_FILE:cat_arena>"
        "-DALLOW=${CMAKE_CURRENT_SOURCE_DIR}/tests/vulkan_baseline_imports.txt" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_vulkan_imports.cmake")
    endif()
    # N2.1: test-only device-simulation layer (never installed, never loaded by the game). It makes the
    # installed driver report an older API / fewer extensions / fewer feature bits so the 1.2 baseline
    # is exercised as a real 1.2 device would exercise it.
    if(UNIX AND NOT APPLE AND NOT ANDROID)
      set(cat_vk_sim_default ON)
    else()
      set(cat_vk_sim_default OFF)
    endif()
    option(CAT_BUILD_VK_SIM_LAYER "Build the test-only Vulkan device-simulation layer" ${cat_vk_sim_default})
    if(CAT_BUILD_VK_SIM_LAYER)
      set(cat_vk_sim_dir "${CMAKE_CURRENT_BINARY_DIR}/vk_sim_layer")
      add_library(VkLayer_cat_device_sim MODULE tests/vk_device_sim_layer.cpp)
      target_include_directories(VkLayer_cat_device_sim SYSTEM PRIVATE "${CAT_VULKAN_HEADERS}")
      target_link_libraries(VkLayer_cat_device_sim PRIVATE cat_warnings)
      set_target_properties(VkLayer_cat_device_sim PROPERTIES CXX_VISIBILITY_PRESET hidden LIBRARY_OUTPUT_DIRECTORY "${cat_vk_sim_dir}")
      file(GENERATE OUTPUT "${cat_vk_sim_dir}/VkLayer_cat_device_sim.json" CONTENT
"{\n  \"file_format_version\": \"1.0.0\",\n  \"layer\": {\n    \"name\": \"VK_LAYER_CAT_device_sim\",\n    \"type\": \"INSTANCE\",\n    \"library_path\": \"$<TARGET_FILE:VkLayer_cat_device_sim>\",\n    \"api_version\": \"1.4.309\",\n    \"implementation_version\": \"1\",\n    \"description\": \"Cat Arena test-only device simulation (older API / fewer extensions)\"\n  }\n}\n")
      add_dependencies(cat_arena_path_tests VkLayer_cat_device_sim)
      add_executable(cat_arena_vk_sim_selftest tests/vk_sim_layer_selftest.cpp)
      target_include_directories(cat_arena_vk_sim_selftest SYSTEM PRIVATE "${CAT_VULKAN_HEADERS}")
      target_link_libraries(cat_arena_vk_sim_selftest PRIVATE ${Vulkan_LIBRARIES} cat_warnings)
      add_dependencies(cat_arena_vk_sim_selftest VkLayer_cat_device_sim)
      add_test(NAME vk_sim_layer_selftest COMMAND cat_arena_vk_sim_selftest "${cat_vk_sim_dir}")
      set_tests_properties(vk_sim_layer_selftest PROPERTIES SKIP_RETURN_CODE 77 LABELS gpu)
      add_test(NAME render_path_simulated_devices COMMAND cat_arena_path_tests "${CMAKE_CURRENT_BINARY_DIR}/shaders" "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" --sim "${cat_vk_sim_dir}")
      set_tests_properties(render_path_simulated_devices PROPERTIES SKIP_RETURN_CODE 77 LABELS gpu TIMEOUT 1800)
    endif()
  endif()
endif()

# N1 native asset IO: no JS runtime or third-party parser in the game.
add_library(cat_arena_assets src/assets.cpp)
target_include_directories(cat_arena_assets PUBLIC include)
target_link_libraries(cat_arena_assets PRIVATE cat_warnings)
add_executable(cat_arena_asset_check src/asset_check.cpp)
target_link_libraries(cat_arena_asset_check PRIVATE cat_arena_assets cat_warnings)
if(BUILD_TESTING)
  add_executable(cat_arena_asset_tests tests/asset_tests.cpp)
  target_link_libraries(cat_arena_asset_tests PRIVATE cat_arena_assets cat_warnings)
  add_test(NAME asset_format COMMAND cat_arena_asset_tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/helios-crucible/map.cadata")
    add_test(NAME baked_maps COMMAND cat_arena_asset_check "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
  endif()
endif()

# N2 render-scene preparation: GPU/SDL independent. stb codecs are vendored (third_party/stb,
# MIT/public domain, see PROVENANCE.txt) and compiled in their own target without -Werror.
add_library(cat_image_codec STATIC src/image_codec.cpp)
target_include_directories(cat_image_codec PUBLIC include)
target_include_directories(cat_image_codec SYSTEM PRIVATE third_party/stb)
target_compile_features(cat_image_codec PUBLIC cxx_std_20)
add_library(cat_arena_scene src/scene.cpp src/md3.cpp)
target_include_directories(cat_arena_scene PUBLIC include)
target_link_libraries(cat_arena_scene PUBLIC cat_arena_assets cat_image_codec PRIVATE cat_warnings)
if(BUILD_TESTING)
  add_executable(cat_arena_scene_tests tests/scene_tests.cpp)
  target_link_libraries(cat_arena_scene_tests PRIVATE cat_arena_scene cat_warnings)
  add_test(NAME scene_units COMMAND cat_arena_scene_tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/helios-crucible/map.cadata")
    add_test(NAME helios_scene COMMAND cat_arena_scene_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
  endif()
endif()

# N3 physics and player traversal: GPU/SDL independent, deterministic 120 Hz. Floating-point
# contraction (fused multiply-add) is disabled so every platform evaluates the same IEEE operations.
add_library(cat_arena_physics src/physics_world.cpp src/player.cpp src/navigation.cpp src/combat.cpp src/enemies.cpp)
target_include_directories(cat_arena_physics PUBLIC include)
target_link_libraries(cat_arena_physics PUBLIC cat_arena_assets PRIVATE cat_warnings)
if(MSVC)
  target_compile_options(cat_arena_physics PRIVATE /fp:precise)
else()
  target_compile_options(cat_arena_physics PRIVATE -ffp-contract=off)
endif()
if(BUILD_TESTING)
  add_executable(cat_arena_physics_tests tests/physics_tests.cpp)
  target_link_libraries(cat_arena_physics_tests PRIVATE cat_arena_physics cat_warnings)
  add_test(NAME physics_units COMMAND cat_arena_physics_tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/helios-crucible/map.cadata")
    add_test(NAME physics_legacy_trajectories COMMAND cat_arena_physics_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n3/helios_trajectories.bin")
    add_test(NAME physics_all_maps COMMAND cat_arena_physics_tests --maps "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
    # N4: legacy trajectories for the other three maps (floors, bridges, ramps and pads of each map).
    foreach(cat_map bastion-keep reactor-bastion q3-gallery)
      add_test(NAME physics_legacy_trajectories_${cat_map} COMMAND cat_arena_physics_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n4/${cat_map}_trajectories.bin" ${cat_map})
    endforeach()
  endif()
endif()

# N6.5 cat/js_math.hpp (correctly rounded sin, cos, tan, atan2, pow used by the simulation and the
# effects): compared bit for bit with Math.* results recorded from Chromium.
if(BUILD_TESTING)
  add_executable(cat_arena_js_math_tests tests/js_math_tests.cpp)
  target_include_directories(cat_arena_js_math_tests PRIVATE include)
  target_link_libraries(cat_arena_js_math_tests PRIVATE cat_warnings)
  if(MSVC)
    target_compile_options(cat_arena_js_math_tests PRIVATE /fp:precise)
  else()
    target_compile_options(cat_arena_js_math_tests PRIVATE -ffp-contract=off)
  endif()
  add_test(NAME js_math_browser COMMAND cat_arena_js_math_tests "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/js_math/chromium_math.bin")
endif()

# N6 enemies, waves, AI and enemy projectiles (part of cat_arena_physics): unit tests plus a replay of
# tapes recorded from the legacy game on every map.
if(BUILD_TESTING)
  add_executable(cat_arena_enemy_tests tests/enemy_tests.cpp)
  target_link_libraries(cat_arena_enemy_tests PRIVATE cat_arena_physics cat_warnings)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/helios-crucible/map.cadata")
    add_test(NAME enemy_units COMMAND cat_arena_enemy_tests --units "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
  else()
    add_test(NAME enemy_units COMMAND cat_arena_enemy_tests)
  endif()
  foreach(cat_map helios-crucible bastion-keep reactor-bastion q3-gallery)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/${cat_map}/map.cadata" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/${cat_map}_enemies.bin")
      add_test(NAME enemy_legacy_tapes_${cat_map} COMMAND cat_arena_enemy_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" ${cat_map} "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/${cat_map}_enemies.bin")
    endif()
  endforeach()
endif()

# N7 audio: decoders (stb_vorbis, dr_mp3; own target without the strict warnings), the WebAudio-style
# mixer and the legacy sound rules. GPU/SDL free; the desktop shell plays it through SDL audio.
add_library(cat_audio_codec STATIC src/audio_codec.cpp)
target_include_directories(cat_audio_codec PUBLIC include)
target_compile_features(cat_audio_codec PUBLIC cxx_std_20)
target_link_libraries(cat_audio_codec PUBLIC cat_arena_physics)
if(MSVC)
  target_compile_definitions(cat_audio_codec PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(cat_audio_codec PRIVATE /W0 /utf-8)
else()
  target_compile_options(cat_audio_codec PRIVATE -w)
endif()
add_library(cat_arena_audio src/audio_mixer.cpp src/audio_game.cpp)
target_link_libraries(cat_arena_audio PUBLIC cat_arena_physics cat_audio_codec PRIVATE cat_warnings)
if(BUILD_TESTING)
  add_executable(cat_arena_audio_tests tests/audio_tests.cpp)
  target_link_libraries(cat_arena_audio_tests PRIVATE cat_arena_audio cat_warnings)
  add_test(NAME audio_webaudio_parity COMMAND cat_arena_audio_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n7" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n7")
endif()

# N7 UI: settings model, signed-distance-field text (stb_truetype in its own target), canvas, HUD and
# menus. GPU/SDL free; the renderer draws the resulting triangles.
add_library(cat_ui_font STATIC src/ui_font.cpp)
target_include_directories(cat_ui_font PUBLIC include)
target_compile_features(cat_ui_font PUBLIC cxx_std_20)
target_link_libraries(cat_ui_font PUBLIC cat_arena_scene)
if(MSVC)
  target_compile_options(cat_ui_font PRIVATE /W0 /utf-8)   # the code point list is UTF-8
else()
  target_compile_options(cat_ui_font PRIVATE -w)
endif()
add_library(cat_arena_ui src/game_settings.cpp src/ui_canvas.cpp src/ui_hud.cpp src/menu.cpp)
target_link_libraries(cat_arena_ui PUBLIC cat_ui_font cat_arena_scene PRIVATE cat_warnings)
if(BUILD_TESTING)
  add_executable(cat_arena_ui_tests tests/ui_tests.cpp)
  target_link_libraries(cat_arena_ui_tests PRIVATE cat_arena_ui cat_warnings)
  add_test(NAME ui_menus_settings_hud COMMAND cat_arena_ui_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n7" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n7/legacy_settings.json")
endif()

# N8 networking: JSON, sockets, WebSocket client (ws:// here, wss:// through WinHTTP on Windows), HTTP GET
# and the relay server (port of reference/multiplayer/server.js). No third-party library.
add_library(cat_arena_net src/json.cpp src/net.cpp src/net_winhttp.cpp src/relay.cpp)
target_include_directories(cat_arena_net PUBLIC include)
target_compile_features(cat_arena_net PUBLIC cxx_std_20)
target_link_libraries(cat_arena_net PRIVATE cat_warnings)
if(WIN32)
  target_link_libraries(cat_arena_net PUBLIC ws2_32 winhttp)
endif()
find_package(Threads REQUIRED)
target_link_libraries(cat_arena_net PUBLIC Threads::Threads)
# N8 co-op: the legacy multiplayer client on top of the network layer and the simulation.
add_library(cat_arena_coop src/coop.cpp)
target_link_libraries(cat_arena_coop PUBLIC cat_arena_net cat_arena_physics PRIVATE cat_warnings)
if(BUILD_TESTING)
  add_executable(cat_arena_coop_tests tests/coop_tests.cpp)
  target_link_libraries(cat_arena_coop_tests PRIVATE cat_arena_coop cat_warnings)
  add_test(NAME coop_multiplayer COMMAND cat_arena_coop_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
  set_tests_properties(coop_multiplayer PROPERTIES TIMEOUT 300)
  if(WIN32)   # the WinHTTP WebSocket / HTTP client (the wss:// and https:// path) against the native relay, without TLS
    add_test(NAME net_relay_protocol_winhttp COMMAND cat_arena_net_tests)
    add_test(NAME coop_multiplayer_winhttp COMMAND cat_arena_coop_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1")
    set_tests_properties(net_relay_protocol_winhttp coop_multiplayer_winhttp PROPERTIES ENVIRONMENT "CAT_NET_WINHTTP=1" TIMEOUT 300)
  endif()
endif()
if(BUILD_TESTING)
  add_executable(cat_arena_net_tests tests/net_tests.cpp)
  target_link_libraries(cat_arena_net_tests PRIVATE cat_arena_net cat_warnings)
  add_test(NAME net_relay_protocol COMMAND cat_arena_net_tests)
  # Interoperability with the ORIGINAL Node.js relay (reference/multiplayer/server.js): only when node and
  # its `ws` package are available (CAT_NODE_WS_DIR = a node_modules folder containing ws).
  find_program(CAT_NODE node)
  set(CAT_NODE_WS_DIR "" CACHE PATH "node_modules directory that contains the ws package (interop test with server.js)")
  if(CAT_NODE AND CAT_NODE_WS_DIR AND EXISTS "${CAT_NODE_WS_DIR}/ws")
    add_test(NAME net_interop_legacy_relay COMMAND "${CAT_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/n8/run_with_legacy_relay.cjs"
             "${CMAKE_CURRENT_SOURCE_DIR}/../reference/multiplayer/server.js" "${CAT_NODE_WS_DIR}" $<TARGET_FILE:cat_arena_net_tests> --external)
    set_tests_properties(net_interop_legacy_relay PROPERTIES TIMEOUT 120)
    add_test(NAME coop_interop_legacy_relay COMMAND "${CAT_NODE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/n8/run_with_legacy_relay.cjs"
             "${CMAKE_CURRENT_SOURCE_DIR}/../reference/multiplayer/server.js" "${CAT_NODE_WS_DIR}" $<TARGET_FILE:cat_arena_coop_tests> "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" --external)
    set_tests_properties(coop_interop_legacy_relay PROPERTIES TIMEOUT 120)
  endif()
endif()

# N5 weapon presentation (viewmodel pose, muzzle flash, tracers, impacts, explosions): GPU-free.
add_library(cat_arena_fx src/weapon_fx.cpp src/hud.cpp src/enemy_fx.cpp)
target_link_libraries(cat_arena_fx PUBLIC cat_arena_physics PRIVATE cat_warnings)
if(MSVC)
  target_compile_options(cat_arena_fx PRIVATE /fp:precise)
else()
  target_compile_options(cat_arena_fx PRIVATE -ffp-contract=off)
endif()

if(BUILD_TESTING)
  add_executable(cat_arena_fx_tests tests/fx_tests.cpp)
  target_link_libraries(cat_arena_fx_tests PRIVATE cat_arena_fx cat_warnings)
  add_test(NAME weapon_fx_units COMMAND cat_arena_fx_tests)
  add_executable(cat_arena_hud_tests tests/hud_tests.cpp)
  target_link_libraries(cat_arena_hud_tests PRIVATE cat_arena_fx cat_warnings)
  add_test(NAME hud_units COMMAND cat_arena_hud_tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/helios-crucible/map.cadata")
    add_test(NAME weapon_fx_legacy_tapes COMMAND cat_arena_fx_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n5/helios_combat.bin"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n5/helios_weapon_fx.bin")
  endif()
  # N6 enemy presentation (poses, death animation, instance lists, enemy effects, Marine Cat metadata):
  # unit tests plus a replay of the legacy animation on every map.
  add_executable(cat_arena_enemy_fx_tests tests/enemy_fx_tests.cpp)
  target_link_libraries(cat_arena_enemy_fx_tests PRIVATE cat_arena_fx cat_warnings)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n6/marine.emesh" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/helios-crucible/map.cadata")
    add_test(NAME enemy_fx_units COMMAND cat_arena_enemy_fx_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n6")
  endif()
  foreach(cat_map helios-crucible bastion-keep reactor-bastion q3-gallery)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/${cat_map}/map.cadata" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/${cat_map}_poses.bin")
      add_test(NAME enemy_fx_legacy_animation_${cat_map} COMMAND cat_arena_enemy_fx_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" ${cat_map}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/${cat_map}_enemies.bin" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n6/${cat_map}_poses.bin")
    endif()
  endforeach()
endif()

# N5 weapons, projectiles, damage and pickups (part of cat_arena_physics): unit tests plus a replay of
# tapes recorded from the legacy game.
if(BUILD_TESTING)
  add_executable(cat_arena_combat_tests tests/combat_tests.cpp)
  target_link_libraries(cat_arena_combat_tests PRIVATE cat_arena_physics cat_warnings)
  add_test(NAME combat_units COMMAND cat_arena_combat_tests)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/helios-crucible/map.cadata")
    add_test(NAME combat_legacy_tapes COMMAND cat_arena_combat_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n5/helios_combat.bin")
  endif()
endif()

# N4 stacked navigation (part of cat_arena_physics): graph, adjacency and routes compared with the
# legacy game for every map.
if(BUILD_TESTING)
  add_executable(cat_arena_nav_tests tests/nav_tests.cpp)
  target_link_libraries(cat_arena_nav_tests PRIVATE cat_arena_physics cat_warnings)
  foreach(cat_map helios-crucible bastion-keep reactor-bastion q3-gallery)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1/maps/${cat_map}/map.cadata")
      add_test(NAME navigation_${cat_map} COMMAND cat_arena_nav_tests "${CMAKE_CURRENT_SOURCE_DIR}/../assets/n1" ${cat_map}
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/n4/${cat_map}_navigation.bin")
    endif()
  endforeach()
endif()
# N6.5: the Windows import library for vulkan-1.dll, made here with llvm-lib when it is installed
# (on Windows the configure step itself makes and links it).
if(BUILD_TESTING AND NOT WIN32)
  find_program(CAT_LLVM_LIB NAMES llvm-lib)
  find_program(CAT_LLVM_NM NAMES llvm-nm)
  if(CAT_LLVM_LIB AND CAT_LLVM_NM)
    add_test(NAME windows_vulkan_import_library COMMAND ${CMAKE_COMMAND} "-DTOOL=${CAT_LLVM_LIB}" "-DNM=${CAT_LLVM_NM}"
      "-DALLOW=${CMAKE_CURRENT_SOURCE_DIR}/tests/vulkan_baseline_imports.txt" "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/vulkan-import-check"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/vulkan_import_library_check.cmake")
  endif()
endif()
# N6.5: desktop tests that need a Vulkan device or a window carry the label "gpu", so that a build
# machine without a GPU (a GitHub-hosted Windows runner) can run everything else: ctest -LE gpu.
if(BUILD_TESTING AND CAT_BUILD_DESKTOP)
  add_test(NAME window_input_selftest COMMAND cat_arena --frames 100 --input-selftest --weapons 4,2)
  set_tests_properties(window_input_selftest PROPERTIES RUN_SERIAL TRUE)   # synthetic X11 input: no other window may take focus
  add_test(NAME window_controls_selftest COMMAND cat_arena --controls-selftest)
  set_tests_properties(window_controls_selftest PROPERTIES TIMEOUT 300)
  # N7: the menus with synthetic key events: home, maps, loading, game, pause, settings (FOV, volume), main menu, exit
  add_test(NAME window_menu_selftest COMMAND cat_arena --menu-selftest)
  set_tests_properties(window_menu_selftest PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)   # needs keyboard focus and mouse capture: no other window may take them
  # N8: hosts through the menus on the built-in relay (port 8787), three in-process guests join over TCP
  add_test(NAME window_coop_selftest COMMAND cat_arena --coop-selftest)
  set_tests_properties(window_coop_selftest PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
  get_property(cat_all_tests DIRECTORY PROPERTY TESTS)
  foreach(cat_test IN LISTS cat_all_tests)
    if(cat_test MATCHES "^(render_|window_|vk_sim)")
      get_test_property(${cat_test} LABELS cat_labels)
      if(NOT cat_labels OR NOT "gpu" IN_LIST cat_labels)
        set_property(TEST ${cat_test} APPEND PROPERTY LABELS gpu)
      endif()
    endif()
  endforeach()
endif()
