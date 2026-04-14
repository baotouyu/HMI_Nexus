include_guard(GLOBAL)

option(HMI_NEXUS_ENABLE_CURL "Enable libcurl support for HTTP/HTTPS requests" ON)
option(HMI_NEXUS_USE_VENDORED_CURL
    "Prefer local vendored curl source or dl archive over system libcurl"
    ON)
set(HMI_NEXUS_CURL_DIR "${CMAKE_SOURCE_DIR}/third_party/curl" CACHE PATH
    "Path to vendored libcurl source")
set(HMI_NEXUS_CURL_ARCHIVE "${CMAKE_SOURCE_DIR}/dl/curl-curl-8_19_0.tar.gz" CACHE FILEPATH
    "Path to local libcurl source archive")

if(HMI_NEXUS_ENABLE_CURL)
    set(_hmi_nexus_curl_source "")
    set(_hmi_nexus_curl_allow_vendored ON)

    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND AND OPENSSL_VERSION VERSION_LESS "3.0.0")
        set(_hmi_nexus_curl_allow_vendored OFF)
        message(WARNING
            "Vendored curl 8.19.0 requires OpenSSL >= 3.0.0, current is ${OPENSSL_VERSION}; "
            "falling back to system libcurl")
    elseif(CMAKE_CROSSCOMPILING AND NOT OPENSSL_FOUND)
        set(_hmi_nexus_curl_allow_vendored OFF)
        message(STATUS
            "Cross toolchain OpenSSL was not found; vendored libcurl is disabled and "
            "the HTTP client will fall back to stub mode unless a target libcurl is provided")
    endif()

    if(HMI_NEXUS_USE_VENDORED_CURL AND _hmi_nexus_curl_allow_vendored)
        if(EXISTS "${HMI_NEXUS_CURL_DIR}/CMakeLists.txt")
            set(_hmi_nexus_curl_source "${HMI_NEXUS_CURL_DIR}")
            message(STATUS "Vendored libcurl source found under ${HMI_NEXUS_CURL_DIR}")
        elseif(EXISTS "${HMI_NEXUS_CURL_ARCHIVE}")
            hmi_nexus_extract_archive_to_vendor("${HMI_NEXUS_CURL_ARCHIVE}"
                "${HMI_NEXUS_CURL_DIR}"
                "${HMI_NEXUS_CURL_DIR}/CMakeLists.txt"
                "libcurl")
            if(EXISTS "${HMI_NEXUS_CURL_DIR}/CMakeLists.txt")
                set(_hmi_nexus_curl_source "${HMI_NEXUS_CURL_DIR}")
                message(STATUS "Vendored libcurl source found under ${HMI_NEXUS_CURL_DIR}")
            endif()
        endif()
    endif()

    if(_hmi_nexus_curl_source)
        set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        set(BUILD_STATIC_CURL OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
        set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
        set(CURL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
        set(CURL_USE_OPENSSL ON CACHE BOOL "" FORCE)
        set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
        set(CURL_BROTLI OFF CACHE BOOL "" FORCE)
        set(CURL_ZSTD OFF CACHE BOOL "" FORCE)
        set(HTTP_ONLY ON CACHE BOOL "" FORCE)

        add_subdirectory("${_hmi_nexus_curl_source}" "${CMAKE_BINARY_DIR}/third_party/curl"
            EXCLUDE_FROM_ALL)
        if(TARGET CURL::libcurl)
            add_library(hmi_nexus_curl INTERFACE)
            target_link_libraries(hmi_nexus_curl INTERFACE CURL::libcurl)
            add_library(hmi_nexus::curl ALIAS hmi_nexus_curl)
            set(HMI_NEXUS_HAS_CURL 1)
            message(STATUS "Vendored libcurl enabled from ${_hmi_nexus_curl_source}")
        else()
            message(WARNING "Vendored libcurl source is present but CURL::libcurl target is missing")
        endif()
    else()
        find_package(CURL QUIET)
        if(CURL_FOUND)
            add_library(hmi_nexus_curl INTERFACE)
            target_link_libraries(hmi_nexus_curl INTERFACE CURL::libcurl)
            add_library(hmi_nexus::curl ALIAS hmi_nexus_curl)
            set(HMI_NEXUS_HAS_CURL 1)
            message(STATUS "System libcurl enabled")
        else()
            message(STATUS "libcurl not found; HTTP client will run in stub mode")
        endif()
    endif()
endif()

if(NOT HMI_NEXUS_ENABLE_CURL)
    message(STATUS "libcurl support disabled")
endif()
