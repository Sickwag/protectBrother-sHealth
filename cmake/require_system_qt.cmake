if(NOT BROHEALTH_USE_SYSTEM_QT)
    message(FATAL_ERROR
        "DEB/RPM packages must be built with the distribution's Qt libraries. "
        "Configure a fresh build directory with -DBROHEALTH_USE_SYSTEM_QT=ON.")
endif()
