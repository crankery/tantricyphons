# Generated Cmake Pico project file

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Initialise pico_sdk from installed location
# (note this can come from environment, CMake cache etc)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(tantricyphons C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# Preserve packed assets in flash; only the generated C source lives in build/.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
file(GLOB MONK_ASSETS CONFIGURE_DEPENDS "${CMAKE_CURRENT_LIST_DIR}/assets/monk-*.bin")
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/monk_assets.c
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/embed_monk.py
            ${CMAKE_CURRENT_LIST_DIR}/assets ${CMAKE_CURRENT_BINARY_DIR}/monk_assets.c
    DEPENDS ${MONK_ASSETS} ${CMAKE_CURRENT_LIST_DIR}/tools/embed_monk.py
    VERBATIM
)

# Add executable. Default name is the project name, version 0.1

add_executable(tantricyphons
    # App
    src/app/tantricyphons.c

    # Audio
    src/audio/soundCore.c
    src/audio/voice.c
    src/audio/voice_control.c

    # Controls
    src/controls/input.c
    src/controls/cv_input.c
    src/controls/cv_pitch.c
    src/controls/vowel_controls.c
    src/controls/routing.c
    src/controls/ui_controls.c
    src/controls/encoder_steps.c
    src/controls/calibration.c

    # Midi
    src/midi/midi.c
    src/midi/midi_uart.c

    # Display
    src/display/display.c
    src/display/monk.c
    src/display/menu.c

    # Settings
    src/settings/settings.c
    src/settings/settings_format.c
    src/settings/settings_flash.c

    # Diagnostics
    src/diagnostics/audio_debug.c
    src/diagnostics/usb_console.c
    src/diagnostics/console_command.c

    # Hardware
    src/hardware/dac.c

    # Messaging
    src/messaging/messenger.c

    # Generated packed artwork
    ${CMAKE_CURRENT_BINARY_DIR}/monk_assets.c
)

option(TANTRICYPHONS_AUDIO_DEBUG "Send audio diagnostics over UART0 TX GP0 once/sec" ON)
if(TANTRICYPHONS_AUDIO_DEBUG)
    target_compile_definitions(tantricyphons PRIVATE
        TANTRICYPHONS_AUDIO_DEBUG=1
    )
endif()

option(TANTRICYPHONS_USB_CONSOLE "Enable the interactive USB serial console" ON)
if(TANTRICYPHONS_USB_CONSOLE)
    target_compile_definitions(tantricyphons PRIVATE
        TANTRICYPHONS_USB_CONSOLE=1
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=0
        PICO_STDIO_DEADLOCK_TIMEOUT_MS=0
    )
endif()

set(TANTRICYPHONS_SAMPLE_RATE "44100" CACHE STRING "Audio sample rate in Hz")
set_property(CACHE TANTRICYPHONS_SAMPLE_RATE PROPERTY STRINGS 22050 32000 44100)
if(NOT TANTRICYPHONS_SAMPLE_RATE MATCHES "^[1-9][0-9]*$")
    message(FATAL_ERROR "TANTRICYPHONS_SAMPLE_RATE must be an integer")
endif()
if(TANTRICYPHONS_SAMPLE_RATE LESS 8000 OR TANTRICYPHONS_SAMPLE_RATE GREATER 48000)
    message(FATAL_ERROR "TANTRICYPHONS_SAMPLE_RATE must be between 8000 and 48000 Hz")
endif()
target_compile_definitions(tantricyphons PRIVATE SAMPLE_RATE=${TANTRICYPHONS_SAMPLE_RATE})

target_compile_options(tantricyphons PRIVATE
    -mfpu=fpv5-sp-d16
    -mfloat-abi=softfp
)

pico_set_program_name(tantricyphons "tantricyphons")
pico_set_program_version(tantricyphons "0.1")

# Generate PIO header
pico_generate_pio_header(tantricyphons ${CMAKE_CURRENT_LIST_DIR}/pio/dac.pio)
pico_generate_pio_header(tantricyphons ${CMAKE_CURRENT_LIST_DIR}/pio/encoder.pio)
pico_generate_pio_header(tantricyphons ${CMAKE_CURRENT_LIST_DIR}/pio/mcp3208.pio)

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(tantricyphons 0)
if(TANTRICYPHONS_USB_CONSOLE)
    pico_enable_stdio_usb(tantricyphons 1)
else()
    pico_enable_stdio_usb(tantricyphons 0)
endif()

# Add the standard library to the build
target_link_libraries(tantricyphons
        pico_stdlib
        pico_multicore
        pico_flash)

# Add the standard include files to the build
target_include_directories(tantricyphons PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

# Add any user requested libraries
target_link_libraries(tantricyphons 
        hardware_flash
        hardware_adc
        hardware_clocks
        hardware_uart
        hardware_spi
        hardware_i2c
        hardware_dma
        hardware_pio
        hardware_timer
        hardware_watchdog
        )

pico_add_extra_outputs(tantricyphons)


add_custom_command(TARGET tantricyphons POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tools/check_flash_size.py
            ${CMAKE_CURRENT_BINARY_DIR}/tantricyphons.bin
    VERBATIM
)
