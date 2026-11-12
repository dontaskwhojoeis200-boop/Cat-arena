# N6.5: import library for vulkan-1.dll without a Vulkan SDK.
# vulkan-1.dll is the Vulkan loader that every Windows Vulkan driver installs. The engine may link only
# Vulkan 1.0 + WSI commands (tests/vulkan_baseline_imports.txt, enforced by the vulkan_link_imports test);
# a module-definition file listing exactly those commands is turned into vulkan-1.lib by lib.exe (MSVC)
# or llvm-lib (clang-cl / tests on other hosts).
#   cat_vulkan_import_library(<allowlist> <lib tool> <output dir> <result variable>)
function(cat_vulkan_import_library allowlist tool out_dir result_var)
  file(STRINGS "${allowlist}" exports REGEX "^vk[A-Za-z0-9_]+$")
  list(LENGTH exports count)
  if(count LESS 40)
    message(FATAL_ERROR "Only ${count} Vulkan commands in ${allowlist}")
  endif()
  list(JOIN exports "\n  " body)
  file(MAKE_DIRECTORY "${out_dir}")
  file(WRITE "${out_dir}/vulkan-1.def" "LIBRARY vulkan-1.dll\nEXPORTS\n  ${body}\n")
  execute_process(COMMAND "${tool}" /nologo /machine:x64 "/def:${out_dir}/vulkan-1.def" "/out:${out_dir}/vulkan-1.lib"
                  RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE out)
  if(NOT rc EQUAL 0 OR NOT EXISTS "${out_dir}/vulkan-1.lib")
    message(FATAL_ERROR "Could not make vulkan-1.lib with ${tool}: ${out}")
  endif()
  set(${result_var} "${out_dir}/vulkan-1.lib" PARENT_SCOPE)
  set(${result_var}_COUNT ${count} PARENT_SCOPE)
endfunction()
