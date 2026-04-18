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

# create githash used instead of APP_VERSION for debug builds
message("Creating githash.h")
execute_process(COMMAND
        ./create_githash_h
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        )
if(CMAKE_BUILD_TYPE STREQUAL "Debug")       
        add_compile_definitions(USE_GIT_HASH_AS_VERSION)
endif()

# create pio header file for controlling addressable led strips 
add_custom_command(OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/ws2812.py
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio
        COMMAND Pioasm -o python ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio ${CMAKE_CURRENT_LIST_DIR}/generated/ws2812.py
        VERBATIM)

# create pio header file for controlling seven segnment display 
add_custom_command(OUTPUT ${CMAKE_CURRENT_LIST_DIR}/generated/tm1637.py
        DEPENDS ${CMAKE_CURRENT_LIST_DIR}/tm1637.pio
        COMMAND Pioasm -o python ${CMAKE_CURRENT_LIST_DIR}/tm1637.pio ${CMAKE_CURRENT_LIST_DIR}/generated/tm1637.py
        VERBATIM)        

set(common_source_files
        pluto.c
        web.c
        # weather.c
        flash.c
        ssi.c
        cgi.c
        calendar.c
        utility.c
        config.c
        dhcpserver.c
        dnsserver.c
        # led_strip.c
        sdk_callback.c
        watchdog.c
        # message.c
        udp.c
        wifi.c
        usurper_ping.c
        ping_core.c
        )

# ====================================================================================
# pluto common libraries
set(pluto_link_libraries
        pico_cyw43_arch_lwip_sys_freertos
        pico_lwip_freertos
        pico_lwip_api        
        pico_lwip_sntp
        pico_lwip_http
        pico_lwip_mqtt
        pico_stdlib
        pico_rand
        FreeRTOS-Kernel-Heap4
        pico_flash
        hardware_pio
        hardware_i2c
        pico_httpd_content
        )


# pico_w has insufficent RAM to incorporate HTTPS web client (thus tesla powerwall monitoring is disabled on pico_w)
if(NOT PICO_BOARD STREQUAL "pico_w")
        set(pluto_link_libraries ${pluto_link_libraries}        
                pico_lwip_mbedtls
                pico_mbedtls)        
endif()


# ====================================================================================
# remote-switch
set(WEB_FILES)
file(GLOB WEB_FILES "${CMAKE_CURRENT_SOURCE_DIR}/remote-switch/html_files/*.shtml")
pico_add_library(pico_httpd_content NOFLAG)
pico_set_lwip_httpd_content(pico_httpd_content INTERFACE ${WEB_FILES})

set(remote-switch_source_files ${common_source_files}
        remote-switch/worker_tasks.c        
        rmtsw_task.c
        rmtsw_web_ui.c
        rmtsw_relay.c
        mqtt_task.c
)        

add_executable(remote-switch ${remote-switch_source_files})  
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

target_include_directories(remote-switch PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/pico-sdk/lib/lwip/contrib/ports/freertos/include/ 
        if(PICO_BOARD STREQUAL "pico_w")
        ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel/portable/ThirdParty/GCC/RP2040/
        else()
        ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel/portable/ThirdParty/GCC/RP2350_ARM_NTZ/include/
        endif()        
        ${CMAKE_CURRENT_LIST_DIR}/FreeRTOS-Kernel/include/
        ${CMAKE_CURRENT_LIST_DIR}/pico-sdk/src/rp2_common/pico_cyw43_arch/include/
        ${CMAKE_CURRENT_LIST_DIR}/pico-sdk/lib/cyw43-driver/src/
        ${CMAKE_CURRENT_LIST_DIR}/pico-sdk/src/rp2_common/pico_cyw43_driver/include/
        ${CMAKE_CURRENT_LIST_DIR}/pico-sdk/lib/lwip/src/apps/altcp_tls/
        ${CMAKE_CURRENT_LIST_DIR}/pico-sdk/lib/mbedtls/include/
        ${CMAKE_CURRENT_LIST_DIR}/pico-sdk/lib/lwip/contrib/apps/ping/
        )

target_link_libraries(remote-switch ${pluto_link_libraries})

# create uf2 
pico_add_extra_outputs(remote-switch)



