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
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2_w CACHE STRING "Board type")

# Pull in Raspberry Pi Pico SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico_scd30_ap C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

# ----------------------------------------------------------------------------
# Fetch Sensirion SCD30 Driver
# ----------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    sensirion_scd30
    GIT_REPOSITORY https://github.com/Sensirion/embedded-i2c-scd30.git
    GIT_TAG      master
)
FetchContent_MakeAvailable(sensirion_scd30)

# Add executable. Default name is the project name, version 0.1
add_executable(pico_scd30_ap 
    main.c
    dhcpserver/dhcpserver.c
    dnsserver/dnsserver.c
    sensirion_i2c_hal.c
    # Sensirion Driver Source Files
    ${sensirion_scd30_SOURCE_DIR}/scd30_i2c.c
    ${sensirion_scd30_SOURCE_DIR}/sensirion_common.c
    ${sensirion_scd30_SOURCE_DIR}/sensirion_i2c.c
)

pico_set_program_name(pico_scd30_ap "pico_scd30_ap")
pico_set_program_version(pico_scd30_ap "0.1")

# Modify the below lines to enable/disable output over UART/USB
pico_enable_stdio_uart(pico_scd30_ap 0)
pico_enable_stdio_usb(pico_scd30_ap 1)

# Add the standard library to the build
target_link_libraries(pico_scd30_ap
        pico_stdlib
        pico_cyw43_arch_lwip_threadsafe_background
        hardware_i2c
)

# Configure the AP IP Address (192.168.4.1)
pico_configure_ip4_address(pico_scd30_ap PRIVATE
    CYW43_DEFAULT_IP_AP_ADDRESS 192.168.4.1
)

# Add the standard include files to the build
target_include_directories(pico_scd30_ap PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/dhcpserver
        ${CMAKE_CURRENT_LIST_DIR}/dnsserver
        ${sensirion_scd30_SOURCE_DIR}
        ${sensirion_scd30_SOURCE_DIR}/sample-implementations/RaspberryPi_Pico
)

# Add any user requested libraries
target_link_libraries(pico_scd30_ap 
        
        )

pico_add_extra_outputs(pico_scd30_ap)

