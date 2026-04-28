cmake_minimum_required(VERSION 3.13..3.27)

# ====================================================================================
# Set release version
set(APP_VERSION "01.00.03")

# Set build type
set(CMAKE_BUILD_TYPE "Debug")
#set(CMAKE_BUILD_TYPE "Release")

# Set board type
#set(PICO_BOARD pico_w CACHE STRING "Board type")
set(PICO_BOARD pico2_w CACHE STRING "Board type")
# ====================================================================================

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ====================================================================================
# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.1)

set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.1.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
#set(CMAKE_EXECUTE_PROCESS_COMMAND_ECHO STDOUT)

set(PICO_SDK_PATH ${CMAKE_CURRENT_LIST_DIR}/pico-sdk)
set(FREERTOS_KERNEL_PATH ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel)
#set(PICOTOOL_FORCE_FETCH_FROM_GIT 1)

add_compile_definitions(PLUTO_VER="${APP_VERSION}")
add_compile_definitions(PICO_CYW43_ARCH_FREERTOS)
add_compile_definitions(PICO_INCLUDE_RTC_DATETIME)
add_compile_definitions(FAKE_RTC)

# initialize pico-sdk from submodule
include(pico-sdk/pico_sdk_init.cmake)

# set the project name ${PROJECT_NAME}
project(remote-switch C CXX ASM)

# Initialise the Raspberry Pi Pico SDK
pico_sdk_init()

if(NOT PICO_BOARD STREQUAL "pico_w") 
include(FreeRTOS-Kernel/portable/ThirdParty/Community-Supported-Ports/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)
#include(FreeRTOS-Kernel/portable/ThirdParty/GCC/RP2350_ARM_NTZ/FreeRTOS_Kernel_import.cmake)  
else()
include(FreeRTOS_Kernel_import.cmake)
endif()

# create "generated" directory
file(MAKE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}/generated)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")       
        add_compile_definitions(USE_GIT_HASH_AS_VERSION)
endif()

      
# ====================================================================================
# remote-switch

add_executable(remote-switch)
add_subdirectory(pluto)  
add_subdirectory(application)

target_compile_definitions(remote-switch PRIVATE APP_NAME="remote-switch")
target_compile_definitions(remote-switch PRIVATE INCORPORATE_REMOTE_SWITCH)

pico_set_program_name(remote-switch "remote-switch")
pico_set_program_version(remote-switch ${APP_VERSION})

# enable/disable output over UART/USB  ***TODO: had to disable stdio over usb in Release builds as it causes random crashes/resets during startup
if(CMAKE_BUILD_TYPE STREQUAL "Release")
        pico_enable_stdio_usb(remote-switch 0)
        pico_enable_stdio_uart(remote-switch 0)        
else() 
        pico_enable_stdio_usb(remote-switch 0)
        pico_enable_stdio_uart(remote-switch 1)        
endif()

# create uf2 
pico_add_extra_outputs(remote-switch)



