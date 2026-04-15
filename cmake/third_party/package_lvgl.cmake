include_guard(GLOBAL)

option(HMI_NEXUS_ENABLE_LVGL "Enable vendored LVGL support for UI rendering" ON)
option(HMI_NEXUS_ENABLE_LVGL_DEMOS "Build LVGL demo library when vendored LVGL is enabled" ON)
option(HMI_NEXUS_USE_VENDORED_LVGL
    "Prefer local vendored LVGL source or dl archive over disabling LVGL"
    ON)
set(HMI_NEXUS_LVGL_DIR "${CMAKE_SOURCE_DIR}/third_party/lvgl" CACHE PATH
    "Path to vendored LVGL source")
set(HMI_NEXUS_LVGL_ARCHIVE "${CMAKE_SOURCE_DIR}/dl/lvgl-9.1.0.tar.gz" CACHE FILEPATH
    "Path to local LVGL source archive")
set(HMI_NEXUS_LVGL_CONF "${CMAKE_SOURCE_DIR}/config/lvgl/lv_conf.h" CACHE FILEPATH
    "Path to LVGL configuration header")

if(HMI_NEXUS_ENABLE_LVGL)
    set(_hmi_nexus_lvgl_source "")

    if(HMI_NEXUS_USE_VENDORED_LVGL)
        if(EXISTS "${HMI_NEXUS_LVGL_DIR}/CMakeLists.txt")
            set(_hmi_nexus_lvgl_source "${HMI_NEXUS_LVGL_DIR}")
            message(STATUS "Vendored LVGL source found under ${HMI_NEXUS_LVGL_DIR}")
        elseif(EXISTS "${HMI_NEXUS_LVGL_ARCHIVE}")
            hmi_nexus_extract_archive_to_vendor("${HMI_NEXUS_LVGL_ARCHIVE}"
                "${HMI_NEXUS_LVGL_DIR}"
                "${HMI_NEXUS_LVGL_DIR}/CMakeLists.txt"
                "LVGL")
            if(EXISTS "${HMI_NEXUS_LVGL_DIR}/CMakeLists.txt")
                set(_hmi_nexus_lvgl_source "${HMI_NEXUS_LVGL_DIR}")
                message(STATUS "Vendored LVGL source found under ${HMI_NEXUS_LVGL_DIR}")
            endif()
        endif()
    endif()

    if(_hmi_nexus_lvgl_source)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(LV_CONF_BUILD_DISABLE_EXAMPLES ON CACHE BOOL "" FORCE)
        if(HMI_NEXUS_ENABLE_LVGL_DEMOS)
            set(LV_CONF_BUILD_DISABLE_DEMOS OFF CACHE BOOL "" FORCE)
        else()
            set(LV_CONF_BUILD_DISABLE_DEMOS ON CACHE BOOL "" FORCE)
        endif()
        set(LV_CONF_BUILD_DISABLE_THORVG_INTERNAL ON CACHE BOOL "" FORCE)
        set(LV_CONF_SKIP OFF CACHE BOOL "" FORCE)
        if(EXISTS "${HMI_NEXUS_LVGL_CONF}")
            set(LV_CONF_PATH "${HMI_NEXUS_LVGL_CONF}" CACHE STRING "" FORCE)
        endif()

        add_subdirectory("${_hmi_nexus_lvgl_source}" "${CMAKE_BINARY_DIR}/third_party/lvgl"
            EXCLUDE_FROM_ALL)
        if(TARGET lvgl)
            add_library(hmi_nexus_lvgl INTERFACE)
            target_link_libraries(hmi_nexus_lvgl INTERFACE lvgl)
            add_library(hmi_nexus::lvgl ALIAS hmi_nexus_lvgl)
            set(HMI_NEXUS_HAS_LVGL 1)
            message(STATUS "Vendored LVGL enabled from ${_hmi_nexus_lvgl_source}")
        else()
            message(WARNING "Vendored LVGL source is present but lvgl target is missing")
        endif()
        if(TARGET lvgl_demos)
            add_library(hmi_nexus_lvgl_demos INTERFACE)
            target_link_libraries(hmi_nexus_lvgl_demos INTERFACE lvgl_demos)
            add_library(hmi_nexus::lvgl_demos ALIAS hmi_nexus_lvgl_demos)
            set(HMI_NEXUS_HAS_LVGL_DEMOS 1)
            message(STATUS "Vendored LVGL demos enabled")
        endif()
    else()
        message(STATUS "LVGL source not found; UI port will remain in stub mode")
    endif()
else()
    message(STATUS "LVGL support disabled")
endif()
