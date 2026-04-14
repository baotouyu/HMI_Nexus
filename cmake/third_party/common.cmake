include_guard(GLOBAL)

function(hmi_nexus_extract_archive_to_vendor archive_path destination_dir marker_file vendor_name)
    if(EXISTS "${marker_file}")
        return()
    endif()

    if(NOT EXISTS "${archive_path}")
        return()
    endif()

    if(EXISTS "${destination_dir}")
        message(FATAL_ERROR
            "Vendored ${vendor_name} directory exists but is incomplete: ${destination_dir}\n"
            "Please remove it or place a valid source tree there.")
    endif()

    find_program(HMI_NEXUS_TAR_EXECUTABLE tar)
    if(NOT HMI_NEXUS_TAR_EXECUTABLE)
        message(FATAL_ERROR
            "Unable to extract vendored ${vendor_name}: 'tar' command was not found")
    endif()

    file(MAKE_DIRECTORY "${destination_dir}")
    execute_process(
        COMMAND "${HMI_NEXUS_TAR_EXECUTABLE}" -xf "${archive_path}" --strip-components=1 -C "${destination_dir}"
        RESULT_VARIABLE _hmi_nexus_extract_result
        ERROR_VARIABLE _hmi_nexus_extract_error
        OUTPUT_QUIET
    )
    if(NOT _hmi_nexus_extract_result EQUAL 0)
        message(FATAL_ERROR
            "Failed to extract vendored ${vendor_name} archive: ${archive_path}\n"
            "${_hmi_nexus_extract_error}")
    endif()

    message(STATUS "Vendored ${vendor_name} extracted to ${destination_dir}")
endfunction()
