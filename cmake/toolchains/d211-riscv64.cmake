set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    D211_SDK_ROOT
    D211_OUTPUT_DIR
    D211_TOOLCHAIN_PREFIX
    D211_TOOLCHAIN_BIN_DIR
    D211_SYSROOT)

set(D211_SDK_ROOT "$ENV{D211_SDK_ROOT}" CACHE PATH
    "Path to the ArtInChip D211 SDK root")
set(D211_OUTPUT_DIR "output/d211_ki_linux_board_002" CACHE STRING
    "Relative output directory inside the D211 SDK")
set(D211_TOOLCHAIN_PREFIX "riscv64-unknown-linux-gnu" CACHE STRING
    "Cross compiler prefix")

if(NOT D211_SDK_ROOT)
    message(FATAL_ERROR
        "D211_SDK_ROOT is not set. Pass -DD211_SDK_ROOT=/path/to/d211 "
        "or export D211_SDK_ROOT before running CMake.")
endif()

if(NOT DEFINED D211_TOOLCHAIN_BIN_DIR OR D211_TOOLCHAIN_BIN_DIR STREQUAL "")
    set(D211_TOOLCHAIN_BIN_DIR
        "${D211_SDK_ROOT}/${D211_OUTPUT_DIR}/host/opt/ext-toolchain/bin"
        CACHE PATH "Path to the D211 cross toolchain bin directory")
endif()

if(NOT DEFINED D211_SYSROOT OR D211_SYSROOT STREQUAL "")
    set(D211_SYSROOT
        "${D211_SDK_ROOT}/${D211_OUTPUT_DIR}/host/riscv64-linux-gnu/sysroot"
        CACHE PATH "Path to the D211 target sysroot")
endif()

set(_d211_cc "${D211_TOOLCHAIN_BIN_DIR}/${D211_TOOLCHAIN_PREFIX}-gcc")
set(_d211_cxx "${D211_TOOLCHAIN_BIN_DIR}/${D211_TOOLCHAIN_PREFIX}-g++")
set(_d211_ar "${D211_TOOLCHAIN_BIN_DIR}/${D211_TOOLCHAIN_PREFIX}-ar")
set(_d211_ranlib "${D211_TOOLCHAIN_BIN_DIR}/${D211_TOOLCHAIN_PREFIX}-ranlib")
set(_d211_strip "${D211_TOOLCHAIN_BIN_DIR}/${D211_TOOLCHAIN_PREFIX}-strip")

foreach(_tool IN ITEMS "${_d211_cc}" "${_d211_cxx}" "${_d211_ar}" "${_d211_ranlib}" "${_d211_strip}")
    if(NOT EXISTS "${_tool}")
        message(FATAL_ERROR "Missing D211 toolchain file: ${_tool}")
    endif()
endforeach()

if(NOT EXISTS "${D211_SYSROOT}")
    message(FATAL_ERROR "Missing D211 sysroot: ${D211_SYSROOT}")
endif()

set(CMAKE_C_COMPILER "${_d211_cc}")
set(CMAKE_CXX_COMPILER "${_d211_cxx}")
set(CMAKE_AR "${_d211_ar}")
set(CMAKE_RANLIB "${_d211_ranlib}")
set(CMAKE_STRIP "${_d211_strip}")
set(CMAKE_SYSROOT "${D211_SYSROOT}")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
