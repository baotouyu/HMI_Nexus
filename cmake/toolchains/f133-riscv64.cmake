set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR riscv64)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(HMI_NEXUS_TARGET_F133 ON CACHE BOOL
    "Internal marker: build HMI_Nexus for Allwinner F133" FORCE)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
    F133_SDK_ROOT
    F133_TOOLCHAIN_PREFIX
    F133_TOOLCHAIN_BIN_DIR
    F133_SYSROOT)

set(F133_SDK_ROOT "$ENV{F133_SDK_ROOT}" CACHE PATH
    "Path to the Allwinner Tina-Linux SDK root or the f133_lvgl demo root")
set(F133_TOOLCHAIN_PREFIX "riscv64-unknown-linux-gnu" CACHE STRING
    "Cross compiler prefix")

if(NOT F133_SDK_ROOT)
    message(FATAL_ERROR
        "F133_SDK_ROOT is not set. Pass -DF133_SDK_ROOT=/path/to/Tina-Linux "
        "or export F133_SDK_ROOT before running CMake.")
endif()

set(_f133_tina_root "")
set(_f133_demo_root "")
if(EXISTS "${F133_SDK_ROOT}/package/gui/littlevgl-8/lv_drivers/display")
    set(_f133_tina_root "${F133_SDK_ROOT}")
endif()
if(EXISTS "${F133_SDK_ROOT}/f133_lvgl")
    set(_f133_demo_root "${F133_SDK_ROOT}/f133_lvgl")
elseif(EXISTS "${F133_SDK_ROOT}/toolchain/gcc/linux-x86/riscv/toolchain-thead-glibc")
    set(_f133_demo_root "${F133_SDK_ROOT}")
    if(NOT _f133_tina_root AND EXISTS "${F133_SDK_ROOT}/../package/gui/littlevgl-8/lv_drivers/display")
        get_filename_component(_f133_tina_root "${F133_SDK_ROOT}/.." ABSOLUTE)
    endif()
endif()

if(NOT _f133_tina_root)
    message(FATAL_ERROR
        "Unable to locate Tina-Linux package headers from F133_SDK_ROOT=${F133_SDK_ROOT}")
endif()
if(NOT _f133_demo_root)
    message(FATAL_ERROR
        "Unable to locate the f133_lvgl demo/toolchain directory from F133_SDK_ROOT=${F133_SDK_ROOT}")
endif()

set(F133_TINA_ROOT "${_f133_tina_root}" CACHE PATH
    "Resolved Tina-Linux SDK root")
set(F133_DEMO_ROOT "${_f133_demo_root}" CACHE PATH
    "Resolved f133_lvgl demo root")

if(NOT DEFINED F133_TOOLCHAIN_BIN_DIR OR F133_TOOLCHAIN_BIN_DIR STREQUAL "")
    set(F133_TOOLCHAIN_BIN_DIR
        "${F133_DEMO_ROOT}/toolchain/gcc/linux-x86/riscv/toolchain-thead-glibc/riscv64-glibc-gcc-thead_20200702/bin"
        CACHE PATH "Path to the F133 cross toolchain bin directory")
endif()

if(NOT DEFINED F133_SYSROOT OR F133_SYSROOT STREQUAL "")
    set(F133_SYSROOT
        "${F133_DEMO_ROOT}/toolchain/gcc/linux-x86/riscv/toolchain-thead-glibc/riscv64-glibc-gcc-thead_20200702/sysroot"
        CACHE PATH "Path to the F133 target sysroot")
endif()

set(_f133_cc "${F133_TOOLCHAIN_BIN_DIR}/${F133_TOOLCHAIN_PREFIX}-gcc")
set(_f133_cxx "${F133_TOOLCHAIN_BIN_DIR}/${F133_TOOLCHAIN_PREFIX}-g++")
set(_f133_ar "${F133_TOOLCHAIN_BIN_DIR}/${F133_TOOLCHAIN_PREFIX}-ar")
set(_f133_ranlib "${F133_TOOLCHAIN_BIN_DIR}/${F133_TOOLCHAIN_PREFIX}-ranlib")
set(_f133_strip "${F133_TOOLCHAIN_BIN_DIR}/${F133_TOOLCHAIN_PREFIX}-strip")

foreach(_tool IN ITEMS "${_f133_cc}" "${_f133_cxx}" "${_f133_ar}" "${_f133_ranlib}" "${_f133_strip}")
    if(NOT EXISTS "${_tool}")
        message(FATAL_ERROR "Missing F133 toolchain file: ${_tool}")
    endif()
endforeach()

if(NOT EXISTS "${F133_SYSROOT}")
    message(FATAL_ERROR "Missing F133 sysroot: ${F133_SYSROOT}")
endif()

set(CMAKE_C_COMPILER "${_f133_cc}")
set(CMAKE_CXX_COMPILER "${_f133_cxx}")
set(CMAKE_AR "${_f133_ar}")
set(CMAKE_RANLIB "${_f133_ranlib}")
set(CMAKE_STRIP "${_f133_strip}")
set(CMAKE_SYSROOT "${F133_SYSROOT}")

set(CMAKE_FIND_ROOT_PATH "${CMAKE_SYSROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
