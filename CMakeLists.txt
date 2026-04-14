cmake_minimum_required(VERSION 3.20)

project(HMI_Nexus VERSION 0.1.0 LANGUAGES C CXX)

find_package(Threads REQUIRED)

include(cmake/options.cmake)
include(cmake/third_party.cmake)

set(HMI_NEXUS_HAS_D211_GE2D 0)
set(HMI_NEXUS_HAS_D211_MPP_DECODER 0)
set(HMI_NEXUS_HAS_SUNXI_G2D 0)
if(HMI_NEXUS_ENABLE_D211_GE2D)
    find_path(HMI_NEXUS_D211_GE2D_INCLUDE_DIR
        NAMES mpp_ge.h
        PATH_SUFFIXES usr/local/include)
    find_library(HMI_NEXUS_D211_GE2D_LIBRARY
        NAMES mpp_ge
        PATH_SUFFIXES usr/local/lib)
    find_library(HMI_NEXUS_D211_MPP_BASE_LIBRARY
        NAMES mpp_base
        PATH_SUFFIXES usr/local/lib)

    if(HMI_NEXUS_D211_GE2D_INCLUDE_DIR AND
       HMI_NEXUS_D211_GE2D_LIBRARY AND
       HMI_NEXUS_D211_MPP_BASE_LIBRARY)
        add_library(hmi_nexus_d211_ge2d INTERFACE)
        target_include_directories(hmi_nexus_d211_ge2d INTERFACE
            "${HMI_NEXUS_D211_GE2D_INCLUDE_DIR}")
        target_link_libraries(hmi_nexus_d211_ge2d INTERFACE
            "${HMI_NEXUS_D211_GE2D_LIBRARY}"
            "${HMI_NEXUS_D211_MPP_BASE_LIBRARY}")
        add_library(hmi_nexus::d211_ge2d ALIAS hmi_nexus_d211_ge2d)
        set(HMI_NEXUS_HAS_D211_GE2D 1)
        message(STATUS "D211 GE2D acceleration enabled")
    else()
        message(STATUS "D211 GE2D acceleration unavailable in current toolchain/sysroot")
    endif()
else()
    message(STATUS "D211 GE2D acceleration disabled")
endif()

set(HMI_NEXUS_F133_SDK_ROOT "")
if(DEFINED F133_SDK_ROOT AND NOT F133_SDK_ROOT STREQUAL "")
    set(HMI_NEXUS_F133_SDK_ROOT "${F133_SDK_ROOT}")
elseif(DEFINED ENV{F133_SDK_ROOT} AND NOT "$ENV{F133_SDK_ROOT}" STREQUAL "")
    set(HMI_NEXUS_F133_SDK_ROOT "$ENV{F133_SDK_ROOT}")
endif()

if(HMI_NEXUS_ENABLE_SUNXI_G2D)
    set(HMI_NEXUS_SUNXI_DISPLAY_INCLUDE_DIR "")
    set(HMI_NEXUS_SUNXI_UAPI_INCLUDE_DIR "")
    set(HMI_NEXUS_SUNXI_UAPI_LIBRARY "")
    set(_hmi_nexus_f133_tina_root "")
    set(_hmi_nexus_f133_demo_root "")
    set(_hmi_nexus_f133_sysroot "")

    if(HMI_NEXUS_F133_SDK_ROOT)
        if(EXISTS "${HMI_NEXUS_F133_SDK_ROOT}/package/gui/littlevgl-8/lv_drivers/display")
            set(_hmi_nexus_f133_tina_root "${HMI_NEXUS_F133_SDK_ROOT}")
        elseif(EXISTS "${HMI_NEXUS_F133_SDK_ROOT}/../package/gui/littlevgl-8/lv_drivers/display")
            get_filename_component(_hmi_nexus_f133_tina_root
                "${HMI_NEXUS_F133_SDK_ROOT}/.."
                ABSOLUTE)
        endif()

        if(EXISTS "${HMI_NEXUS_F133_SDK_ROOT}/f133_lvgl")
            set(_hmi_nexus_f133_demo_root "${HMI_NEXUS_F133_SDK_ROOT}/f133_lvgl")
        elseif(EXISTS "${HMI_NEXUS_F133_SDK_ROOT}/toolchain/gcc/linux-x86/riscv/toolchain-thead-glibc")
            set(_hmi_nexus_f133_demo_root "${HMI_NEXUS_F133_SDK_ROOT}")
        elseif(_hmi_nexus_f133_tina_root AND
               EXISTS "${_hmi_nexus_f133_tina_root}/f133_lvgl")
            set(_hmi_nexus_f133_demo_root "${_hmi_nexus_f133_tina_root}/f133_lvgl")
        endif()
    endif()

    if(DEFINED F133_SYSROOT AND NOT F133_SYSROOT STREQUAL "")
        set(_hmi_nexus_f133_sysroot "${F133_SYSROOT}")
    elseif(DEFINED ENV{F133_SYSROOT} AND NOT "$ENV{F133_SYSROOT}" STREQUAL "")
        set(_hmi_nexus_f133_sysroot "$ENV{F133_SYSROOT}")
    elseif(_hmi_nexus_f133_demo_root AND
           EXISTS "${_hmi_nexus_f133_demo_root}/toolchain/gcc/linux-x86/riscv/toolchain-thead-glibc/riscv64-glibc-gcc-thead_20200702/sysroot")
        set(_hmi_nexus_f133_sysroot
            "${_hmi_nexus_f133_demo_root}/toolchain/gcc/linux-x86/riscv/toolchain-thead-glibc/riscv64-glibc-gcc-thead_20200702/sysroot")
    endif()

    if(_hmi_nexus_f133_tina_root AND
       EXISTS "${_hmi_nexus_f133_tina_root}/package/gui/littlevgl-8/lv_drivers/display/g2d_driver_enh.h")
        set(HMI_NEXUS_SUNXI_DISPLAY_INCLUDE_DIR
            "${_hmi_nexus_f133_tina_root}/package/gui/littlevgl-8/lv_drivers/display")
    endif()

    if(_hmi_nexus_f133_tina_root AND
       EXISTS "${_hmi_nexus_f133_tina_root}/package/allwinner/libuapi/src/include/ion_mem_alloc.h")
        set(HMI_NEXUS_SUNXI_UAPI_INCLUDE_DIR
            "${_hmi_nexus_f133_tina_root}/package/allwinner/libuapi/src/include")
    elseif(_hmi_nexus_f133_tina_root AND
           EXISTS "${_hmi_nexus_f133_tina_root}/out/f133-mq_r/staging_dir/target/usr/include/ion_mem_alloc.h")
        set(HMI_NEXUS_SUNXI_UAPI_INCLUDE_DIR
            "${_hmi_nexus_f133_tina_root}/out/f133-mq_r/staging_dir/target/usr/include")
    endif()

    foreach(_hmi_nexus_sunxi_uapi_candidate IN ITEMS
            "${_hmi_nexus_f133_sysroot}/usr/lib64xthead/lp64d/libuapi.so"
            "${_hmi_nexus_f133_sysroot}/lib64xthead/lp64d/libuapi.so"
            "${_hmi_nexus_f133_tina_root}/out/f133-mq_r/staging_dir/target/usr/lib/libuapi.so")
        if(_hmi_nexus_sunxi_uapi_candidate AND
           EXISTS "${_hmi_nexus_sunxi_uapi_candidate}")
            set(HMI_NEXUS_SUNXI_UAPI_LIBRARY "${_hmi_nexus_sunxi_uapi_candidate}")
            break()
        endif()
    endforeach()

    if(HMI_NEXUS_SUNXI_DISPLAY_INCLUDE_DIR AND
       HMI_NEXUS_SUNXI_UAPI_INCLUDE_DIR AND
       HMI_NEXUS_SUNXI_UAPI_LIBRARY)
        add_library(hmi_nexus_sunxi_g2d INTERFACE)
        target_include_directories(hmi_nexus_sunxi_g2d INTERFACE
            "${HMI_NEXUS_SUNXI_DISPLAY_INCLUDE_DIR}"
            "${HMI_NEXUS_SUNXI_UAPI_INCLUDE_DIR}")
        target_link_libraries(hmi_nexus_sunxi_g2d INTERFACE
            "${HMI_NEXUS_SUNXI_UAPI_LIBRARY}")
        add_library(hmi_nexus::sunxi_g2d ALIAS hmi_nexus_sunxi_g2d)
        set(HMI_NEXUS_HAS_SUNXI_G2D 1)
        message(STATUS "Sunxi G2D acceleration enabled")
    else()
        message(STATUS "Sunxi G2D acceleration unavailable in current SDK/sysroot")
    endif()
else()
    message(STATUS "Sunxi G2D acceleration disabled")
endif()

if(HMI_NEXUS_ENABLE_D211_MPP_DECODER)
    find_path(HMI_NEXUS_D211_MPP_DECODER_INCLUDE_DIR
        NAMES mpp_decoder.h
        PATH_SUFFIXES usr/local/include)
    find_library(HMI_NEXUS_D211_MPP_DECODER_LIBRARY
        NAMES mpp_decoder
        PATH_SUFFIXES usr/local/lib)
    find_library(HMI_NEXUS_D211_MPP_DECODER_BASE_LIBRARY
        NAMES mpp_base
        PATH_SUFFIXES usr/local/lib)

    if(HMI_NEXUS_D211_MPP_DECODER_INCLUDE_DIR AND
       HMI_NEXUS_D211_MPP_DECODER_LIBRARY AND
       HMI_NEXUS_D211_MPP_DECODER_BASE_LIBRARY)
        add_library(hmi_nexus_d211_mpp_decoder INTERFACE)
        target_include_directories(hmi_nexus_d211_mpp_decoder INTERFACE
            "${HMI_NEXUS_D211_MPP_DECODER_INCLUDE_DIR}")
        target_link_libraries(hmi_nexus_d211_mpp_decoder INTERFACE
            "${HMI_NEXUS_D211_MPP_DECODER_LIBRARY}"
            "${HMI_NEXUS_D211_MPP_DECODER_BASE_LIBRARY}")
        add_library(hmi_nexus::d211_mpp_decoder ALIAS hmi_nexus_d211_mpp_decoder)
        set(HMI_NEXUS_HAS_D211_MPP_DECODER 1)
        message(STATUS "D211 MPP image decoder enabled")
    else()
        message(STATUS "D211 MPP image decoder unavailable in current toolchain/sysroot")
    endif()
else()
    message(STATUS "D211 MPP image decoder disabled")
endif()

set(HMI_NEXUS_SOURCES
    source/src/app/application.cpp
    source/src/common/json/cjson_parser.cpp
    source/src/device/display/d211_ge2d_accel_2d.cpp
    source/src/device/display/fbdev_display_backend.cpp
    source/src/device/display/headless_display_backend.cpp
    source/src/device/display/null_accel_2d_backend.cpp
    source/src/device/display/sunxi_g2d_accel_2d.cpp
    source/src/device/runtime.cpp
    source/src/net/http_async_client.cpp
    source/src/net/http_client.cpp
    source/src/net/mqtt_client.cpp
    source/src/net/tls_context.cpp
    source/src/net/topic_router.cpp
    source/src/net/wifi_manager.cpp
    source/src/net/websocket_client.cpp
    source/src/service/cloud_service.cpp
    source/src/service/connectivity_service.cpp
    source/src/service/device_service.cpp
    source/src/service/ota_service.cpp
    source/src/system/config_center.cpp
    source/src/system/event_bus.cpp
    source/src/system/logger.cpp
    source/src/system/ui_dispatcher.cpp
    source/src/ui/app.cpp
    source/src/ui/lvgl/d211/d211_lvgl_ge2d.c
    source/src/ui/lvgl/d211/d211_lvgl_mpp_decoder.c
    source/src/ui/lvgl/f133/f133_lvgl_g2d.c
    source/src/ui/lvgl/linux_touch_input.cpp
    source/src/ui/lvgl/lvgl_port.cpp
    source/src/ui/screen.cpp
    source/src/ui/screen_manager.cpp
    source/src/ui/screens/ge2d_diagnostics_screen.cpp
    source/src/ui/screens/home_screen.cpp
    source/src/ui/screens/lvgl_benchmark_demo_screen.cpp
    source/src/ui/screens/lvgl_widgets_demo_screen.cpp
    source/src/ui/theme_manager.cpp
)

add_library(hmi_nexus STATIC ${HMI_NEXUS_SOURCES})

target_compile_features(hmi_nexus PUBLIC cxx_std_17)
target_include_directories(hmi_nexus
    PUBLIC source/include
    PRIVATE .
)
target_link_libraries(hmi_nexus PUBLIC Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(hmi_nexus PUBLIC pthread)
endif()
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    target_link_libraries(hmi_nexus PUBLIC stdc++fs)
endif()
if(NOT DEFINED HMI_NEXUS_TARGET_F133)
    set(HMI_NEXUS_TARGET_F133 OFF)
endif()
target_compile_definitions(hmi_nexus PUBLIC
    HMI_NEXUS_PLATFORM_LINUX=1
    HMI_NEXUS_HAS_CJSON=${HMI_NEXUS_HAS_CJSON}
    HMI_NEXUS_HAS_CURL=${HMI_NEXUS_HAS_CURL}
    HMI_NEXUS_HAS_D211_GE2D=${HMI_NEXUS_HAS_D211_GE2D}
    HMI_NEXUS_HAS_D211_MPP_DECODER=${HMI_NEXUS_HAS_D211_MPP_DECODER}
    HMI_NEXUS_HAS_LVGL=${HMI_NEXUS_HAS_LVGL}
    HMI_NEXUS_HAS_LVGL_DEMOS=${HMI_NEXUS_HAS_LVGL_DEMOS}
    HMI_NEXUS_HAS_SUNXI_G2D=${HMI_NEXUS_HAS_SUNXI_G2D}
    HMI_NEXUS_TARGET_F133=$<BOOL:${HMI_NEXUS_TARGET_F133}>
)

if(TARGET hmi_nexus_cjson)
    target_link_libraries(hmi_nexus PRIVATE hmi_nexus_cjson)
endif()

if(TARGET hmi_nexus_curl)
    target_link_libraries(hmi_nexus PRIVATE hmi_nexus_curl)
endif()

if(TARGET hmi_nexus_lvgl)
    target_link_libraries(hmi_nexus PUBLIC hmi_nexus_lvgl)
endif()

if(TARGET hmi_nexus_lvgl_demos)
    target_link_libraries(hmi_nexus PUBLIC hmi_nexus_lvgl_demos)
endif()

if(TARGET hmi_nexus_d211_ge2d)
    target_link_libraries(hmi_nexus PRIVATE hmi_nexus_d211_ge2d)
endif()

if(TARGET hmi_nexus_d211_mpp_decoder)
    target_link_libraries(hmi_nexus PRIVATE hmi_nexus_d211_mpp_decoder)
endif()

if(TARGET hmi_nexus_sunxi_g2d)
    target_link_libraries(hmi_nexus PRIVATE hmi_nexus_sunxi_g2d)
endif()

if(HMI_NEXUS_BUILD_DEMO)
    add_executable(hmi_nexus_demo source/examples/demo_panel/main.cpp)
    target_link_libraries(hmi_nexus_demo PRIVATE hmi_nexus)
endif()

if(HMI_NEXUS_BUILD_EXAMPLES)
    add_executable(hmi_nexus_http_example source/examples/http_example/main.cpp)
    target_link_libraries(hmi_nexus_http_example PRIVATE hmi_nexus)

    add_executable(hmi_nexus_http_async_example source/examples/http_async_example/main.cpp)
    target_link_libraries(hmi_nexus_http_async_example PRIVATE hmi_nexus)

    add_executable(hmi_nexus_logger_example source/examples/logger_example/main.cpp)
    target_link_libraries(hmi_nexus_logger_example PRIVATE hmi_nexus)

    add_executable(hmi_nexus_json_example source/examples/json_example/main.cpp)
    target_link_libraries(hmi_nexus_json_example PRIVATE hmi_nexus)

    add_executable(hmi_nexus_wifi_example source/examples/wifi_example/main.cpp)
    target_link_libraries(hmi_nexus_wifi_example PRIVATE hmi_nexus)
endif()

message(STATUS "Configuring HMI_Nexus for Linux-only runtime")
