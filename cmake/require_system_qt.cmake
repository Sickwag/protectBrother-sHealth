# Deb/RPM packages must be built against the distribution Qt. A binary built
# against an official Qt SDK references newer ABI symbol versions (for example
# Qt_6.11) and aborts at startup on machines that only ship the distribution Qt.
# The symbol check in cmake/verify_qt_symbols.cmake enforces the same rule on
# the produced binary; this file gives the earlier, clearer error message.
if(NOT BROHEALTH_USE_SYSTEM_QT)
    message(FATAL_ERROR
        "DEB/RPM packages must be built with the distribution's Qt libraries. "
        "Run cmake in a shell without QT_HOME_PATH, remove the build directory "
        "(a cached SDK Qt survives reconfigure), and configure again with "
        "-DBROHEALTH_USE_SYSTEM_QT=ON. Debian/Ubuntu needs qt6-base-dev, "
        "qt6-base-dev-tools and qmake6.")
endif()
