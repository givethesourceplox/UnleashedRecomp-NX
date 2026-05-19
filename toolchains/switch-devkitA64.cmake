# Nintendo Switch/libnx cross toolchain for devkitPro devkitA64.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(UNLEASHED_RECOMP_SWITCH ON CACHE BOOL "Build for Nintendo Switch/libnx")
set(PLUME_PLATFORM_SWITCH ON CACHE BOOL "Build Plume with Nintendo Switch/libnx Vulkan support")

if(NOT DEFINED DEVKITPRO)
  set(DEVKITPRO "/opt/devkitpro" CACHE PATH "devkitPro root")
endif()

set(_DEVKITA64 "${DEVKITPRO}/devkitA64")
set(_DEVKITA64_BIN "${_DEVKITA64}/bin")
set(_DEVKITA64_PREFIX "aarch64-none-elf-")

set(CMAKE_C_COMPILER "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}gcc" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}g++" CACHE FILEPATH "" FORCE)
set(CMAKE_ASM_COMPILER "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}gcc" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}gcc-ar" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}gcc-ranlib" CACHE FILEPATH "" FORCE)
set(CMAKE_NM "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}nm" CACHE FILEPATH "" FORCE)
set(CMAKE_OBJCOPY "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}objcopy" CACHE FILEPATH "" FORCE)
set(CMAKE_STRIP "${_DEVKITA64_BIN}/${_DEVKITA64_PREFIX}strip" CACHE FILEPATH "" FORCE)

set(CMAKE_C_COMPILER_AR "${CMAKE_AR}" CACHE FILEPATH "" FORCE)
set(CMAKE_C_COMPILER_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_AR "${CMAKE_AR}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER_RANLIB "${CMAKE_RANLIB}" CACHE FILEPATH "" FORCE)

set(_SWITCH_ARCH_FLAGS "-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -fPIE -D__SWITCH__ -DNX")
set(_SWITCH_WARNING_FLAGS "-Wno-psabi")
set(CMAKE_C_FLAGS_INIT "-ffunction-sections -fdata-sections ${_SWITCH_ARCH_FLAGS} ${_SWITCH_WARNING_FLAGS}")
set(CMAKE_CXX_FLAGS_INIT "-ffunction-sections -fdata-sections ${_SWITCH_ARCH_FLAGS} ${_SWITCH_WARNING_FLAGS}")
# --allow-multiple-definition: NVK ships two Rust static libs (libnak_rs.a
# and liblibnil.a) that each bundle their own copy of the Rust runtime
# (rust_eh_personality, std::panicking::EMPTY_PANIC, ARGV_INIT_ARRAY).
# Mesa's own link absorbs this internally; for downstream consumers we
# have to tell the linker to take the first definition and move on.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-specs=${DEVKITPRO}/libnx/switch.specs -Wl,--gc-sections -Wl,--allow-multiple-definition")
set(CMAKE_DL_LIBS "")

include_directories(SYSTEM
  "${_DEVKITA64}/include"
  "${DEVKITPRO}/libnx/include"
  "${DEVKITPRO}/portlibs/switch/include"
)

link_directories(
  "${_DEVKITA64}/lib"
  "${DEVKITPRO}/libnx/lib"
  "${DEVKITPRO}/portlibs/switch/lib"
)

set(CMAKE_FIND_ROOT_PATH
  "${_DEVKITA64}"
  "${DEVKITPRO}/libnx"
  "${DEVKITPRO}/portlibs/switch"
)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
