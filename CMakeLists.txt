cmake_minimum_required(VERSION 3.16)
project(ProtectBrothersHealth VERSION 1.2.0 LANGUAGES CXX)
include(GNUInstallDirs)
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Default to the distribution Qt. Building a package against an official Qt SDK
# produces a binary that references symbols such as Qt_6.11 and then aborts on
# machines that only ship the distribution Qt, so opting out must be explicit.
option(BROHEALTH_USE_SYSTEM_QT
    "Build against the distribution Qt (required for portable DEB/RPM packages)"
    ON)

set(BROHEALTH_SYSTEM_QT_PREFIX "/usr" CACHE PATH
    "Distribution Qt prefix used by portable DEB/RPM builds")

if(BROHEALTH_USE_SYSTEM_QT)
    # A package build must not reuse a cached custom Qt configuration.
    unset(Qt6_DIR CACHE)
    unset(Qt6Core_DIR CACHE)
    unset(Qt6Widgets_DIR CACHE)
    unset(Qt6Test_DIR CACHE)
    unset(Qt6DBus_DIR CACHE)
elseif(NOT "$ENV{QT_HOME_PATH}" STREQUAL "")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_HOME_PATH}")
    set(QT_PLUGIN_DIR "$ENV{QT_HOME_PATH}/plugins")
endif()

if(BROHEALTH_USE_SYSTEM_QT AND NOT "$ENV{QT_HOME_PATH}" STREQUAL "")
    message(WARNING
        "QT_HOME_PATH is set, but this build uses the distribution Qt. "
        "Run the build from a shell without QT_HOME_PATH if CMake picks up the SDK.")
endif()

if(BROHEALTH_USE_SYSTEM_QT)
    # Do not let a PATH entry or an SDK environment variable redirect
    # find_package() to a Qt that the target distribution does not ship.
    set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
    set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)
    set(CMAKE_FIND_USE_PACKAGE_REGISTRY FALSE)
    set(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY FALSE)

    # Honour the configured distribution prefix explicitly instead of relying on
    # whatever CMAKE_PREFIX_PATH happens to contain.
    list(APPEND CMAKE_PREFIX_PATH "${BROHEALTH_SYSTEM_QT_PREFIX}")

    # Fail with an actionable message instead of CMake's generic one when the
    # distribution Qt development packages are simply not installed.
    file(GLOB _system_qt_config
        "${BROHEALTH_SYSTEM_QT_PREFIX}/lib/*/cmake/Qt6/Qt6Config.cmake"
        "${BROHEALTH_SYSTEM_QT_PREFIX}/lib/cmake/Qt6/Qt6Config.cmake")
    if(NOT _system_qt_config)
        message(FATAL_ERROR
            "The distribution Qt development packages are missing: no Qt6Config.cmake "
            "was found under ${BROHEALTH_SYSTEM_QT_PREFIX}. "
            "Debian/Ubuntu: sudo apt install qt6-base-dev qt6-base-dev-tools qmake6. "
            "Fedora/RHEL: sudo dnf install qt6-qtbase-devel. "
            "Arch: sudo pacman -S qt6-base. "
            "A build against an official Qt SDK is only meant for local runs; "
            "such a package cannot start on machines that ship the distribution Qt.")
    endif()
    unset(_system_qt_config)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
if(UNIX AND NOT APPLE)
    find_package(Qt6 6.4 REQUIRED COMPONENTS DBus)
endif()
include(CTest)
option(BROHEALTH_REGISTER_NOTIFICATION_TESTS
    "Register tests that display desktop notifications"
    OFF)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()

if(BROHEALTH_USE_SYSTEM_QT)
    unset(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH)
    unset(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH)
    unset(CMAKE_FIND_USE_PACKAGE_REGISTRY)
    unset(CMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY)

    # Every distro package installs its Qt either in /usr or under the sysroot
    # prefix used by cross builds; anything else is a hand-made SDK.
    get_filename_component(_qt_core_dir_real "${Qt6Core_DIR}" REALPATH)
    get_filename_component(_system_qt_prefix_real "${BROHEALTH_SYSTEM_QT_PREFIX}" REALPATH)
    string(FIND "${_qt_core_dir_real}" "${_system_qt_prefix_real}/" _system_qt_index)
    if(NOT _system_qt_index EQUAL 0)
        message(FATAL_ERROR
            "System Qt was requested, but Qt6Core was resolved from '${_qt_core_dir_real}', "
            "which is outside ${_system_qt_prefix_real}. "
            "Install the distribution Qt development packages (Debian/Ubuntu: "
            "qt6-base-dev qt6-base-dev-tools qmake6), run the build in a shell without "
            "QT_HOME_PATH, and use a fresh build directory.")
    endif()
endif()

# Keep the build tree usable with a non-system Qt installation. Installed
# packages use the target system's normal Qt plugin search path.
if(QT_PLUGIN_DIR)
    configure_file(resources/qt.conf "${CMAKE_CURRENT_BINARY_DIR}/qt.conf" @ONLY)
endif()

# A portable package must not depend on Qt newer than the distribution ships.
# The check runs before CPack writes a package and again from CTest.
set(BROHEALTH_QT_RUNTIME_VERSION "${Qt6Core_VERSION}")

# -------------------- Binary --------------------

set(QT_LIB_DIR "")
if(NOT BROHEALTH_USE_SYSTEM_QT AND NOT "$ENV{QT_HOME_PATH}" STREQUAL "")
    set(QT_LIB_DIR "$ENV{QT_HOME_PATH}/lib")
endif()

add_library(brohealth-core STATIC
    include/break_reminder.h
    include/color_temp.h
    include/debt_ledger.h
    include/desktop_notifier.h
    include/mainwindow.h
    include/monitor_ctrl.h
    include/phrase_catalog.h
    include/preset_manager.h
    include/reminder_manager.h
    include/settings_manager.h
    src/break_reminder.cpp
    src/color_temp.cpp
    src/debt_ledger.cpp
    src/desktop_notifier.cpp
    src/mainwindow.cpp
    src/monitor_ctrl.cpp
    src/phrase_catalog.cpp
    src/preset_manager.cpp
    src/reminder_manager.cpp
    src/settings_manager.cpp
)
target_include_directories(brohealth-core
    PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(brohealth-core PUBLIC Qt6::Widgets)
if(UNIX AND NOT APPLE)
    target_link_libraries(brohealth-core PUBLIC Qt6::DBus)
    target_compile_definitions(brohealth-core PRIVATE
        BROHEALTH_HAVE_DBUS=1
        BROHEALTH_BUILD_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}"
        BROHEALTH_BUNDLED_PHRASES_CONF="${PROJECT_SOURCE_DIR}/resources/phrases.conf"
        BROHEALTH_INSTALLED_PHRASES_CONF="${CPACK_PACKAGING_INSTALL_PREFIX}/${CMAKE_INSTALL_DATADIR}/brohealth/phrases.conf")
endif()

add_executable(brohealth
    include/platform_tray.h
    src/main.cpp
    src/platform_tray.cpp
)
target_link_libraries(brohealth PRIVATE brohealth-core)
if(UNIX AND NOT APPLE)
    target_link_libraries(brohealth PRIVATE Qt6::DBus)
    target_compile_definitions(brohealth PRIVATE BROHEALTH_HAVE_DBUS=1)
endif()

set_target_properties(brohealth PROPERTIES
    INSTALL_RPATH ""
    BUILD_RPATH "${QT_LIB_DIR}"
)

# -------------------- Install rules --------------------

set(CPACK_PACKAGE_NAME "protect-brothers-health")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Multi-monitor display adjustment and scheduled break reminders")
set(CPACK_GENERATOR "DEB;RPM")

set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Protect Brother's Health contributors")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_REPLACES "screen-light-adjust")
set(CPACK_DEBIAN_PACKAGE_CONFLICTS "screen-light-adjust")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "x11-xserver-utils, redshift, gammastep, qt6-qpa-plugins, qt6-wayland")
set(CPACK_RPM_PACKAGE_REQUIRES "xrandr, redshift, gammastep, qt6-qtbase-gui, qt6-qtwayland")
set(CPACK_RPM_PACKAGE_OBSOLETES "screen-light-adjust")
set(CPACK_RPM_PACKAGE_AUTOREQ ON)

install(TARGETS brohealth RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "resources/phrases.conf"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/brohealth"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
install(FILES "resources/brohealth.desktop"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)
install(FILES "resources/brohealth.svg"
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps"
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ)

configure_file("${CMAKE_SOURCE_DIR}/cmake/postinst.in"
    "${CMAKE_CURRENT_BINARY_DIR}/postinst" @ONLY)
configure_file("${CMAKE_SOURCE_DIR}/cmake/prerm.in"
    "${CMAKE_CURRENT_BINARY_DIR}/prerm" @ONLY)

set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
    "${CMAKE_CURRENT_BINARY_DIR}/postinst;${CMAKE_CURRENT_BINARY_DIR}/prerm")

include(CPack)

if(BUILD_TESTING)
    add_executable(brohealth-tests
        tests/brohealth_test.cpp
    )
    target_link_libraries(brohealth-tests PRIVATE brohealth-core Qt6::Test)
    target_compile_definitions(brohealth-tests PRIVATE
        BROHEALTH_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    add_test(NAME brohealth-tests COMMAND brohealth-tests)
    set_tests_properties(brohealth-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_test(NAME brohealth-phrase-check
        COMMAND brohealth --check-phrases "${PROJECT_SOURCE_DIR}/resources/phrases.conf")
    set_tests_properties(brohealth-phrase-check PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    if(BROHEALTH_USE_SYSTEM_QT)
        add_test(NAME brohealth-qt-symbols COMMAND "${CMAKE_COMMAND}"
            "-DBINARY=$<TARGET_FILE:brohealth>"
            "-DEXPECTED_QT_VERSION=${BROHEALTH_QT_RUNTIME_VERSION}"
            -P "${PROJECT_SOURCE_DIR}/cmake/verify_qt_symbols.cmake")
    endif()

    add_test(NAME brohealth-packaging-guard COMMAND "${CMAKE_COMMAND}"
        "-DTEST_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/packaging-guard-test"
        "-DPACKAGE_POLICY=${PROJECT_SOURCE_DIR}/cmake/require_system_qt.cmake"
        "-DVERIFIER=${PROJECT_SOURCE_DIR}/cmake/verify_qt_symbols.cmake"
        -P "${PROJECT_SOURCE_DIR}/tests/packaging_guard_test.cmake")

    if(UNIX AND NOT APPLE)
        add_executable(brohealth-notification-test
            tests/notification_test.cpp
        )
        target_link_libraries(brohealth-notification-test PRIVATE brohealth-core)
        if(BROHEALTH_REGISTER_NOTIFICATION_TESTS)
            add_test(NAME brohealth-notification-test
                COMMAND brohealth-notification-test "${PROJECT_SOURCE_DIR}/resources/phrases.conf")
            set_tests_properties(brohealth-notification-test PROPERTIES TIMEOUT 30)
        endif()
    endif()
endif()

# Convenience target: cmake --build --preset <name> --target deb
#
# The Qt symbol check runs before CPack so a package that would only start
# against the development Qt is never written to disk.
add_custom_target(deb
    COMMAND ${CMAKE_COMMAND}
        -DBROHEALTH_USE_SYSTEM_QT=${BROHEALTH_USE_SYSTEM_QT}
        -P "${PROJECT_SOURCE_DIR}/cmake/require_system_qt.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DBINARY=$<TARGET_FILE:brohealth>"
        "-DEXPECTED_QT_VERSION=${BROHEALTH_QT_RUNTIME_VERSION}"
        -P "${PROJECT_SOURCE_DIR}/cmake/verify_qt_symbols.cmake"
    COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS brohealth
    COMMENT "Generating .deb package…")

add_custom_target(rpm
    COMMAND ${CMAKE_COMMAND}
        -DBROHEALTH_USE_SYSTEM_QT=${BROHEALTH_USE_SYSTEM_QT}
        -P "${PROJECT_SOURCE_DIR}/cmake/require_system_qt.cmake"
    COMMAND ${CMAKE_COMMAND}
        "-DBINARY=$<TARGET_FILE:brohealth>"
        "-DEXPECTED_QT_VERSION=${BROHEALTH_QT_RUNTIME_VERSION}"
        -P "${PROJECT_SOURCE_DIR}/cmake/verify_qt_symbols.cmake"
    COMMAND ${CMAKE_CPACK_COMMAND} -G RPM
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    DEPENDS brohealth
    COMMENT "Generating .rpm package…")
