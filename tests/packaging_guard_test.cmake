# Guards the two rules that keep a DEB/RPM from requiring a Qt newer than the
# target distribution ships:
#
#   1. cmake/require_system_qt.cmake rejects packaging an SDK-Qt build.
#   2. cmake/verify_qt_symbols.cmake rejects a binary that references Qt_6.x
#      symbols newer than the distribution Qt, or Qt private API symbols.
#
# The symbol check is exercised with fake objdump output so the test does not
# depend on the Qt installed on the build machine.
file(REMOVE_RECURSE "${TEST_DIRECTORY}")
file(MAKE_DIRECTORY "${TEST_DIRECTORY}/bin")
file(WRITE "${TEST_DIRECTORY}/fake-binary" "")

function(write_fake_objdump name body)
    file(WRITE "${TEST_DIRECTORY}/bin/${name}" "#!/bin/sh\ncat <<'OUT'\n${body}OUT\n")
    execute_process(COMMAND chmod +x "${TEST_DIRECTORY}/bin/${name}" COMMAND_ERROR_IS_FATAL ANY)
endfunction()

write_fake_objdump(objdump-good
    "0000000000000000  w   DF *UND*  0000000000000000  Qt_6.4  _ZN7QObjectC1Ev\n0000000000000000  w   DF *UND*  0000000000000000  Qt_6    _Z8qVersionv\n")
write_fake_objdump(objdump-newer
    "0000000000000000  w   DF *UND*  0000000000000000  Qt_6.11  _ZN7QObjectC1Ev\n")
write_fake_objdump(objdump-private
    "0000000000000000  w   DF *UND*  0000000000000000  Qt_6_PRIVATE_API  _ZN7QWidgetD1Ev\n")

# --- packaging policy -------------------------------------------------------
execute_process(COMMAND "${CMAKE_COMMAND}" -DBROHEALTH_USE_SYSTEM_QT=ON
    -P "${PACKAGE_POLICY}" RESULT_VARIABLE _result)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "A distribution-Qt package build was rejected.")
endif()

execute_process(COMMAND "${CMAKE_COMMAND}" -DBROHEALTH_USE_SYSTEM_QT=OFF
    -P "${PACKAGE_POLICY}" RESULT_VARIABLE _result ERROR_VARIABLE _error)
if(_result EQUAL 0)
    message(FATAL_ERROR "An SDK-Qt build was allowed to produce a package.")
endif()

# --- Qt symbol versions -----------------------------------------------------
# verify_qt_symbols.cmake calls find_program(objdump), so pick which fake
# implementation "objdump" resolves to by copying it over the same name.
function(run_symbol_check case expect_success)
    execute_process(COMMAND cp "${TEST_DIRECTORY}/bin/objdump-${case}"
        "${TEST_DIRECTORY}/bin/objdump" COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND chmod +x "${TEST_DIRECTORY}/bin/objdump"
        COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "PATH=${TEST_DIRECTORY}/bin:$ENV{PATH}"
        "${CMAKE_COMMAND}"
        "-DBINARY=${TEST_DIRECTORY}/fake-binary"
        "-DEXPECTED_QT_VERSION=6.4.2"
        -P "${VERIFIER}"
        RESULT_VARIABLE _result OUTPUT_VARIABLE _output ERROR_VARIABLE _error)
    if(expect_success AND NOT _result EQUAL 0)
        message(FATAL_ERROR "${case}: expected the symbol check to pass, got:\n${_error}")
    endif()
    if(NOT expect_success AND _result EQUAL 0)
        message(FATAL_ERROR "${case}: expected the symbol check to fail:\n${_output}")
    endif()
endfunction()

run_symbol_check(good TRUE)
run_symbol_check(newer FALSE)
run_symbol_check(private FALSE)

message(STATUS "Packaging guards behave as expected.")
