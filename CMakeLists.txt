cmake_minimum_required(VERSION 3.16)
project(ProtectBrothersHealth VERSION 1.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_system_qt_default OFF)
if("$ENV{QT_HOME_PATH}" STREQUAL "")
    set(_system_qt_default ON)
endif()
option(BROHEALTH_USE_SYSTEM_QT
    "Use the distribution Qt installation for portable packages"
    "${_system_qt_default}")

if(BROHEALTH_USE_SYSTEM_QT)
    # A package build must not reuse a cached custom Qt configuration.
    unset(Qt6_DIR CACHE)
    unset(Qt6Core_DIR CACHE)
    unset(Qt6Widgets_DIR CACHE)
    unset(Qt6Test_DIR CACHE)
elseif(NOT "$ENV{QT_HOME_PATH}" STREQUAL "")
    list(APPEND CMAKE_PREFIX_PATH "$ENV{QT_HOME_PATH}")
    set(QT_PLUGIN_DIR "$ENV{QT_HOME_PATH}/plugins")
endif()

if(BROHEALTH_USE_SYSTEM_QT)
    if(DEFINED CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH)
        set(_find_system_environment_path_defined ON)
        set(_find_system_environment_path "${CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH}")
    else()
        set(_find_system_environment_path_defined OFF)
    endif()
    set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Widgets)
include(CTest)

if(BUILD_TESTING)
    find_package(Qt6 6.4 REQUIRED COMPONENTS Test)
endif()

if(BROHEALTH_USE_SYSTEM_QT)
    if(_find_system_environment_path_defined)
        set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH "${_find_system_environment_path}")
    else()
        unset(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH)
    endif()
    unset(_find_system_environment_path_defined)
    unset(_find_system_environment_path)

    if(NOT Qt6Core_DIR MATCHES "^/usr/")
        message(FATAL_ERROR
            "System Qt was requested, but Qt6Core was resolved from '${Qt6Core_DIR}'. "
            "Install the distribution's Qt development package and use a fresh build directory.")
    endif()
endif()

# Keep the build tree usable with a non-system Qt installation. Installed
# packages use the target system's normal Qt plugin search path.
if(QT_PLUGIN_DIR)
    configure_file(resources/qt.conf "${CMAKE_CURRENT_BINARY_DIR}/qt.conf" @ONLY)
endif()

# -------------------- Binary --------------------

set(QT_LIB_DIR "")
if(NOT BROHEALTH_USE_SYSTEM_QT AND NOT "$ENV{QT_HOME_PATH}" STREQUAL "")
    set(QT_LIB_DIR "$ENV{QT_HOME_PATH}/lib")
endif()

add_library(brohealth-core STATIC
    include/break_reminder.h
    include/break_timer.h
    include/color_temp.h
    include/mainwindow.h
    include/monitor_ctrl.h
    include/preset_manager.h
    include/settings_manager.h
    src/break_reminder.cpp
    src/break_timer.cpp
    src/color_temp.cpp
    src/mainwindow.cpp
    src/monitor_ctrl.cpp
    src/preset_manager.cpp
    src/settings_manager.cpp
)
target_include_directories(brohealth-core
    PUBLIC "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(brohealth-core PUBLIC Qt6::Widgets)

add_executable(brohealth src/main.cpp)
target_link_libraries(brohealth PRIVATE brohealth-core)

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
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")

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

include(GNUInstallDirs)
install(TARGETS brohealth RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
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
    add_test(NAME brohealth-tests COMMAND brohealth-tests)
    set_tests_properties(brohealth-tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# Convenience target: cmake --build --preset <name> --target deb
add_custom_target(deb
    COMMAND ${CMAKE_COMMAND}
        -DBROHEALTH_USE_SYSTEM_QT=${BROHEALTH_USE_SYSTEM_QT}
        -P "${PROJECT_SOURCE_DIR}/cmake/require_system_qt.cmake"
    COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating .deb package…")

add_custom_target(rpm
    COMMAND ${CMAKE_COMMAND}
        -DBROHEALTH_USE_SYSTEM_QT=${BROHEALTH_USE_SYSTEM_QT}
        -P "${PROJECT_SOURCE_DIR}/cmake/require_system_qt.cmake"
    COMMAND ${CMAKE_CPACK_COMMAND} -G RPM
    WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    COMMENT "Generating .rpm package…")
