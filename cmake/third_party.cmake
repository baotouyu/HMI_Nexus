include_guard(GLOBAL)

# Aggregates third-party package configuration from per-package modules.
set(HMI_NEXUS_HAS_CJSON 0)
set(HMI_NEXUS_HAS_CURL 0)
set(HMI_NEXUS_HAS_LVGL 0)
set(HMI_NEXUS_HAS_LVGL_DEMOS 0)

set(_HMI_NEXUS_THIRD_PARTY_DIR "${CMAKE_CURRENT_LIST_DIR}/third_party")

include("${_HMI_NEXUS_THIRD_PARTY_DIR}/common.cmake")
include("${_HMI_NEXUS_THIRD_PARTY_DIR}/package_cjson.cmake")
include("${_HMI_NEXUS_THIRD_PARTY_DIR}/package_curl.cmake")
include("${_HMI_NEXUS_THIRD_PARTY_DIR}/package_lvgl.cmake")

unset(_HMI_NEXUS_THIRD_PARTY_DIR)
