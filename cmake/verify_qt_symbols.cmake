# Verifies that a packaged binary only references Qt symbol versions provided by
# the Qt runtime that the distribution package will depend on.
#
# Usage:
#   cmake -DBINARY=<path> -DEXPECTED_QT_VERSION=6.4.2 -P verify_qt_symbols.cmake
#
# A package built against a newer Qt (for example an official Qt SDK in
# QT_HOME_PATH) keeps ABI-versioned symbol references such as Qt_6.11. Those
# references make the binary abort at startup on a machine that only ships
# Qt 6.4, which is exactly the failure this check is meant to stop before the
# .deb is written.

if(NOT DEFINED BINARY OR NOT DEFINED EXPECTED_QT_VERSION)
    message(FATAL_ERROR "verify_qt_symbols.cmake needs BINARY and EXPECTED_QT_VERSION")
endif()
if(NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "Binary not found: ${BINARY}")
endif()

find_program(_objdump NAMES objdump llvm-objdump REQUIRED)
execute_process(COMMAND "${_objdump}" -T "${BINARY}"
    OUTPUT_VARIABLE _dynamic_symbols
    ERROR_VARIABLE _objdump_error
    RESULT_VARIABLE _objdump_result)
if(NOT _objdump_result EQUAL 0)
    message(FATAL_ERROR "objdump -T failed for ${BINARY}: ${_objdump_error}")
endif()

# Symbol versions look like Qt_6, Qt_6.4, Qt_6.4.2 or Qt_6_PRIVATE_API.
string(REGEX MATCHALL "Qt_6[A-Za-z0-9_.]*" _qt_versions "${_dynamic_symbols}")
list(REMOVE_DUPLICATES _qt_versions)
list(SORT _qt_versions)

set(_expected "${EXPECTED_QT_VERSION}")
get_filename_component(_binary_name "${BINARY}" NAME)

set(_private_api FALSE)
set(_too_new "")
foreach(_version IN LISTS _qt_versions)
    if(_version STREQUAL "Qt_6_PRIVATE_API")
        set(_private_api TRUE)
        continue()
    endif()
    string(REGEX REPLACE "^Qt_" "" _numeric "${_version}")
    # Compare only on the major.minor.patch prefix; Qt_6.4.0 style suffixes are
    # older than a plain Qt_6.4 requirement but never newer than the runtime.
    if(_numeric VERSION_GREATER _expected)
        list(APPEND _too_new "${_version}")
    endif()
endforeach()

if(_private_api)
    message(FATAL_ERROR
        "${_binary_name} references Qt private API symbols (Qt_6_PRIVATE_API). "
        "Such a binary only runs against the exact Qt build it was compiled with, "
        "so it cannot be shipped as a distribution package. Rebuild with the "
        "distribution Qt (BROHEALTH_USE_SYSTEM_QT=ON).")
endif()

if(_too_new)
    string(REPLACE ";" ", " _too_new_text "${_too_new}")
    message(FATAL_ERROR
        "${_binary_name} requires Qt symbols newer than ${_expected}: ${_too_new_text}. "
        "The package would fail to start on a machine with the distribution Qt. "
        "Rebuild with the distribution Qt: unset QT_HOME_PATH, use a fresh build "
        "directory and BROHEALTH_USE_SYSTEM_QT=ON.")
endif()

message(STATUS "Qt symbol versions in ${_binary_name}: ${_qt_versions} (limit ${_expected})")
