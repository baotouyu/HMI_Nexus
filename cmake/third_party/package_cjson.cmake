include_guard(GLOBAL)

option(HMI_NEXUS_ENABLE_CJSON "Enable vendored cJSON support when source is present" ON)
set(HMI_NEXUS_CJSON_DIR "${CMAKE_SOURCE_DIR}/third_party/cjson" CACHE PATH
    "Path to vendored cJSON source")

if(HMI_NEXUS_ENABLE_CJSON)
    if(EXISTS "${HMI_NEXUS_CJSON_DIR}/cJSON.c" AND EXISTS "${HMI_NEXUS_CJSON_DIR}/cJSON.h")
        add_library(hmi_nexus_cjson STATIC
            "${HMI_NEXUS_CJSON_DIR}/cJSON.c"
        )
        target_include_directories(hmi_nexus_cjson PUBLIC "${HMI_NEXUS_CJSON_DIR}")
        add_library(hmi_nexus::cjson ALIAS hmi_nexus_cjson)
        set(HMI_NEXUS_HAS_CJSON 1)
        message(STATUS "Vendored cJSON enabled from ${HMI_NEXUS_CJSON_DIR}")
    else()
        message(STATUS "Vendored cJSON not found under ${HMI_NEXUS_CJSON_DIR}; JSON wrapper will run in stub mode")
    endif()
else()
    message(STATUS "Vendored cJSON support disabled")
endif()
