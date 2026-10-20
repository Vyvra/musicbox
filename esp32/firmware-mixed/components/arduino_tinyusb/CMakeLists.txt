cmake_minimum_required(VERSION 3.16)

# Arduino 3.3.x ships TinyUSB as a PREBUILT library inside the board-libs package
# (framework-arduinoespressif32-esp32s3-libs/include/arduino_tinyusb + lib/libarduino_tinyusb.a).
# In a pure-Arduino build PlatformIO's SCons builder links it; in an ESP-IDF build the
# component `arduino_tinyusb` simply does not exist (no CMakeLists in that package), and
# esp32-hal-tinyusb.c / <tusb.h> have nothing to resolve against.
# This wrapper re-exposes that prebuilt archive as a real IDF component.

if(DEFINED ENV{PIOHOME_DIR})
    set(_pio "$ENV{PIOHOME_DIR}")
else()
    set(_pio "$ENV{HOME}/.platformio")
endif()
set(TINYUSB_PKG "${_pio}/packages/framework-arduinoespressif32-esp32s3-libs")

if(NOT EXISTS "${TINYUSB_PKG}/lib/libarduino_tinyusb.a")
    message(FATAL_ERROR "prebuilt Arduino TinyUSB not found in ${TINYUSB_PKG}")
endif()

idf_component_register(
    SRCS "tusb_host_only_stubs.c"
    INCLUDE_DIRS
        "${TINYUSB_PKG}/include/arduino_tinyusb/include"   # tusb_config.h (BOARD_TUH_RHPORT, ...)
        "${TINYUSB_PKG}/include/arduino_tinyusb/tinyusb/src"  # tusb.h, host/, class/, ...
)

target_link_libraries(${COMPONENT_LIB} INTERFACE "${TINYUSB_PKG}/lib/libarduino_tinyusb.a")
