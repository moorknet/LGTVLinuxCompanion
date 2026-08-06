cmake_minimum_required(VERSION 3.20)
project(lgtv-companion
    VERSION 5.6.0
    DESCRIPTION "Control LG webOS TVs used as PC displays"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_GUI "Build the Qt user interface" ON)

# ----------------------------------------------------------- dependencies ---

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Boost REQUIRED)
find_package(nlohmann_json 3.9 REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)

# ext-idle-notify-v1 is a staging protocol; generate its glue with
# wayland-scanner rather than depending on a system-provided binding.
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)

set(IDLE_PROTOCOL_XML
    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-idle-notify/ext-idle-notify-v1.xml")
if(NOT EXISTS "${IDLE_PROTOCOL_XML}")
    message(FATAL_ERROR
        "ext-idle-notify-v1.xml not found at ${IDLE_PROTOCOL_XML}. "
        "Install a newer wayland-protocols.")
endif()

set(IDLE_PROTOCOL_DIR "${CMAKE_CURRENT_BINARY_DIR}/wayland-protocols")
file(MAKE_DIRECTORY "${IDLE_PROTOCOL_DIR}")

add_custom_command(
    OUTPUT "${IDLE_PROTOCOL_DIR}/ext-idle-notify-v1-client-protocol.h"
    COMMAND ${WAYLAND_SCANNER} client-header
            "${IDLE_PROTOCOL_XML}"
            "${IDLE_PROTOCOL_DIR}/ext-idle-notify-v1-client-protocol.h"
    DEPENDS "${IDLE_PROTOCOL_XML}"
    VERBATIM)

add_custom_command(
    OUTPUT "${IDLE_PROTOCOL_DIR}/ext-idle-notify-v1-protocol.c"
    COMMAND ${WAYLAND_SCANNER} private-code
            "${IDLE_PROTOCOL_XML}"
            "${IDLE_PROTOCOL_DIR}/ext-idle-notify-v1-protocol.c"
    DEPENDS "${IDLE_PROTOCOL_XML}"
    VERBATIM)

# ------------------------------------------------------------- lgtv-common ---
# Portable core: device model, webOS JSON api, config, logging, local IPC.

add_library(lgtv-common STATIC
    src/common/event.cpp
    src/common/ipc.cpp
    src/common/log.cpp
    src/common/paths.cpp
    src/common/preferences.cpp
    src/common/tools.cpp
)

target_include_directories(lgtv-common PUBLIC src/common)

target_link_libraries(lgtv-common PUBLIC
    Boost::headers
    nlohmann_json::nlohmann_json
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
)

# Boost.Asio pulls in Boost.System; keep it header-only and drop the deprecated
# implicit io_service aliases so the async code compiles clean.
target_compile_definitions(lgtv-common PUBLIC
    BOOST_ASIO_NO_DEPRECATED
    BOOST_ASIO_DISABLE_BOOST_ARRAY
)

target_compile_options(lgtv-common PRIVATE -Wall -Wextra -Wno-unused-parameter)

# --------------------------------------------------------------- smoketest ---

enable_testing()

add_executable(lgtv-smoketest src/common/smoke_test.cpp)
target_link_libraries(lgtv-smoketest PRIVATE lgtv-common)
add_test(NAME smoketest COMMAND lgtv-smoketest)

# ------------------------------------------------------- platform monitors ---
# logind power events and Wayland idle detection.

add_library(lgtv-platform STATIC
    src/daemon/logind.cpp
    src/daemon/idle_monitor.cpp
    "${IDLE_PROTOCOL_DIR}/ext-idle-notify-v1-protocol.c"
    "${IDLE_PROTOCOL_DIR}/ext-idle-notify-v1-client-protocol.h"
)

target_include_directories(lgtv-platform PUBLIC
    src/daemon
    "${IDLE_PROTOCOL_DIR}"
)

target_link_libraries(lgtv-platform PUBLIC
    lgtv-common
    PkgConfig::SYSTEMD
    PkgConfig::WAYLAND
)

target_compile_options(lgtv-platform PRIVATE -Wall -Wextra -Wno-unused-parameter)

# Interactive harness: prints logind and idle events as they arrive.
add_executable(lgtv-platform-probe src/daemon/platform_probe.cpp)
target_link_libraries(lgtv-platform-probe PRIVATE lgtv-platform)

# ------------------------------------------------------------------ daemon ---

add_executable(lgtv-companion-daemon
    src/daemon/main.cpp
    src/daemon/companion.cpp
    src/daemon/web_os_client.cpp
    src/daemon/button_client.cpp
)

target_link_libraries(lgtv-companion-daemon PRIVATE lgtv-platform)
target_compile_options(lgtv-companion-daemon PRIVATE -Wall -Wextra -Wno-unused-parameter)

install(TARGETS lgtv-companion-daemon RUNTIME DESTINATION bin)

# System scope, not user scope: a --user unit does not exist until login, so the
# display would still be off at the login screen, and it is torn down with the
# graphical session before logind broadcasts PrepareForShutdown.
#
# ExecStart follows the install prefix. User= is deliberately left unset here:
# the service should run as the desktop user, which is only known at install
# time, so the user interface generates the real unit. This copy is for
# distribution packaging, where a drop-in supplies User=.
include(GNUInstallDirs)
set(LGTV_BINDIR "${CMAKE_INSTALL_FULL_BINDIR}")
set(LGTV_USER "%i")
set(LGTV_CONFIG_DIR "%h/.config/lgtv-companion")
set(LGTV_STATE_DIR "%h/.local/state/lgtv-companion")
configure_file(data/lgtv-companion-system.service.in
    "${CMAKE_CURRENT_BINARY_DIR}/lgtv-companion@.service" @ONLY)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/lgtv-companion@.service"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/systemd/system")

# --------------------------------------------------------------------- cli ---
# Standalone command line tool. Talks to the TVs directly over the webOS
# websocket API rather than through the daemon.

add_executable(lgtv-cli src/cli/console.cpp)
target_link_libraries(lgtv-cli PRIVATE lgtv-common)
target_compile_options(lgtv-cli PRIVATE -Wall -Wextra -Wno-unused-parameter)
set_target_properties(lgtv-cli PROPERTIES OUTPUT_NAME lgtvcli)

install(TARGETS lgtv-cli RUNTIME DESTINATION bin)

# --------------------------------------------------------------------- gui ---

if(BUILD_GUI)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Widgets)
    qt_standard_project_setup()

    qt_add_executable(lgtv-gui
        src/gui/main.cpp
        src/gui/main_window.cpp
        src/gui/device_dialog.cpp
        src/gui/options_dialog.cpp
        src/gui/service_manager.cpp
    )

    target_link_libraries(lgtv-gui PRIVATE lgtv-common Qt6::Widgets)
    target_compile_options(lgtv-gui PRIVATE -Wall -Wextra -Wno-unused-parameter)
    set_target_properties(lgtv-gui PROPERTIES OUTPUT_NAME lgtv-companion)

    install(TARGETS lgtv-gui RUNTIME DESTINATION bin)
endif()
