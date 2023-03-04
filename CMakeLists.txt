cmake_minimum_required(VERSION 3.12)

# Pull in SDK (must be before project)
include("$ENV{PICO_SDK_PATH}\\external\\pico_sdk_import.cmake")
include("$ENV{FREERTOS_KERNEL_PATH}\\portable\\ThirdParty\\GCC\\RP2040\\FreeRTOS_Kernel_import.cmake")

project(picow_iot_device C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

set(PICO_BOARD pico_w)
set(WIFI_HOSTNAME "PicoW IoT Device")
set(CLOCK_TIMEZONE -18000)
set(CLOCK_DAYLIGHT_SAVINGS 0)

set(TEMPERATURE_UNITS "C")

add_compile_definitions(PICO_PANIC_FUNCTION=rtos_panic_oled)
add_compile_definitions(DEBUG_LEVEL=2)
add_compile_definitions(NO_JVAL_DEPENDENCY)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

# Initialize the SDK
pico_sdk_init()

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )

configure_file( config.h.in ${CMAKE_BINARY_DIR}/generated/config.h )
include_directories( ${CMAKE_BINARY_DIR}/generated/ ) 

add_executable(picow_iot_device
        main.c iot_tcpclient.c lib/ssd1306.c lib/hashmap.c
        #utils
        utils/debug.c
        #json lib
        lib/json/AllocatorIntf.c lib/json/BaAtoi.c lib/json/BufPrint.c lib/json/JDecoder.c lib/json/JEncoder.c lib/json/JParser.c
        )
        
target_include_directories(picow_iot_device PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/.. # for our common lwipopts
        )
target_link_libraries(picow_iot_device
        pico_cyw43_arch_lwip_sys_freertos
        pico_stdlib
        pico_lwip_iperf
        pico_lwip_sntp
        FreeRTOS-Kernel-Heap4 # FreeRTOS kernel and dynamic heap
        hardware_flash hardware_sync hardware_i2c hardware_rtc hardware_adc
        )

if(MSVC)
    add_definitions(/FI"${CMAKE_CURRENT_SOURCE_DIR}/utils/kernel.h")
else()
    # GCC or Clang
    add_definitions(-include "${CMAKE_CURRENT_SOURCE_DIR}/utils/kernel.h")
endif()

pico_add_extra_outputs(picow_iot_device)