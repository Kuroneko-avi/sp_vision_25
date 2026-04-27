# Optional dependency probes for the future MQTT Dashboard bridge.
# This file must not create targets or make dashboard dependencies required.

find_package(PahoMqttCpp QUIET)
if(PahoMqttCpp_FOUND)
    message(STATUS "Dashboard optional dependency: PahoMqttCpp found.")
else()
    message(STATUS "Dashboard optional dependency: PahoMqttCpp not found; future MqttBridge targets will need it.")
endif()

# cpp-httplib is header-only. The recommended future vendor path is:
#   third_party/httplib.h
find_path(DASHBOARD_CPP_HTTPLIB_INCLUDE_DIR
    NAMES httplib.h
    HINTS
        "${PROJECT_SOURCE_DIR}/third_party"
        "${PROJECT_SOURCE_DIR}/third_party/cpp-httplib"
    PATH_SUFFIXES
        ""
        include
    DOC "Optional include directory containing cpp-httplib httplib.h"
)

if(DASHBOARD_CPP_HTTPLIB_INCLUDE_DIR)
    message(STATUS "Dashboard optional dependency: cpp-httplib header found at ${DASHBOARD_CPP_HTTPLIB_INCLUDE_DIR}.")
else()
    message(STATUS "Dashboard optional dependency: cpp-httplib header not found; future HTTP serve can vendor third_party/httplib.h.")
endif()

mark_as_advanced(DASHBOARD_CPP_HTTPLIB_INCLUDE_DIR)
