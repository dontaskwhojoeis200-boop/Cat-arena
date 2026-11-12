# N6.5 install step: THIRD_PARTY_LICENSES.txt for the game folder, from native/third_party_notices.
# Lists only what is compiled into or shipped with the game. Called by `cmake --install` with
# CAT_NOTICES (notices directory) and CAT_OUT (output file) set.
set(text "Cat Arena: Feral Claw - native engine (C++20 / Vulkan)\nThird-party software compiled into or shipped with this game\n")
string(APPEND text "Build tools (CMake, the C++ compiler, glslang for compiling shaders) are not shipped and are not listed.\n")
foreach(entry
    "SDL3 3.2.10 (SDL3.dll / libSDL3.so.0: window, input, Vulkan surface)|SDL3/LICENSE.txt"
    "stb_image / stb_image_write (image decoding, compiled in)|stb/LICENSE"
    "Khronos Vulkan-Headers 1.4.309 (declarations compiled in, licensed Apache-2.0 OR MIT and used here under MIT, the Vulkan loader itself comes with the graphics driver)|vulkan-headers/MIT.txt")
  string(REPLACE "|" ";" parts "${entry}")
  list(GET parts 0 title)
  list(GET parts 1 file)
  if(NOT EXISTS "${CAT_NOTICES}/${file}")
    message(FATAL_ERROR "Missing third-party notice ${CAT_NOTICES}/${file}")
  endif()
  file(READ "${CAT_NOTICES}/${file}" body)
  string(APPEND text "\n==============================================================================\n${title}\n==============================================================================\n${body}\n")
endforeach()
file(WRITE "${CAT_OUT}" "${text}")
message(STATUS "Installing: ${CAT_OUT}")
