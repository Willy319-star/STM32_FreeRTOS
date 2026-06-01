cmake_minimum_required(VERSION 3.20)

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi-gcc.cmake" CACHE FILEPATH "")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(STM32_FreeRTOS C ASM)

set(APP "freertos_blink" CACHE STRING "Application to build: baremetal_blink or freertos_blink")
set_property(CACHE APP PROPERTY STRINGS baremetal_blink freertos_blink)

set(MCU_FLAGS
    -mcpu=cortex-m4
    -mthumb
)

set(COMMON_DEFINES
    STM32F407xx
    USE_HAL_DRIVER
    HSE_VALUE=8000000U
)

set(COMMON_INCLUDES
    ${CMAKE_CURRENT_SOURCE_DIR}/bsp/include
    ${CMAKE_CURRENT_SOURCE_DIR}/config
    ${CMAKE_CURRENT_SOURCE_DIR}/core/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS/Core/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/CMSIS/Device/ST/STM32F4xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/STM32F4xx_HAL_Driver/Inc
)

set(HAL_SOURCES
    third_party/CMSIS/Device/ST/STM32F4xx/Source/system_stm32f4xx.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_cortex.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_flash_ex.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_gpio.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_pwr_ex.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc.c
    third_party/STM32F4xx_HAL_Driver/Src/stm32f4xx_hal_rcc_ex.c
)

set(BSP_SOURCES
    bsp/src/board.c
    core/src/syscalls.c
    startup/startup_stm32f407xx.s
)

set(FREERTOS_SOURCES
    third_party/FreeRTOS/Source/croutine.c
    third_party/FreeRTOS/Source/event_groups.c
    third_party/FreeRTOS/Source/list.c
    third_party/FreeRTOS/Source/queue.c
    third_party/FreeRTOS/Source/stream_buffer.c
    third_party/FreeRTOS/Source/tasks.c
    third_party/FreeRTOS/Source/timers.c
    third_party/FreeRTOS/Source/portable/GCC/ARM_CM3/port.c
    third_party/FreeRTOS/Source/portable/MemMang/heap_4.c
)

if(APP STREQUAL "baremetal_blink")
    set(APP_SOURCES
        app/baremetal_blink/main.c
        core/src/interrupts_baremetal.c
    )
    set(APP_INCLUDES)
elseif(APP STREQUAL "freertos_blink")
    set(APP_SOURCES
        app/freertos_blink/main.c
        core/src/interrupts_freertos.c
        ${FREERTOS_SOURCES}
    )
    set(APP_INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreeRTOS/Source/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/FreeRTOS/Source/portable/GCC/ARM_CM3
    )
else()
    message(FATAL_ERROR "Unknown APP='${APP}'. Use baremetal_blink or freertos_blink.")
endif()

add_executable(${PROJECT_NAME}.elf
    ${HAL_SOURCES}
    ${BSP_SOURCES}
    ${APP_SOURCES}
)

target_compile_definitions(${PROJECT_NAME}.elf PRIVATE ${COMMON_DEFINES})
target_include_directories(${PROJECT_NAME}.elf PRIVATE ${COMMON_INCLUDES} ${APP_INCLUDES})

target_compile_options(${PROJECT_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -Wall
    -Wextra
    -Wno-unused-parameter
    $<$<COMPILE_LANGUAGE:C>:-std=c11>
)

target_link_options(${PROJECT_NAME}.elf PRIVATE
    ${MCU_FLAGS}
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32F407VETx_FLASH.ld
    -Wl,-Map=${CMAKE_BINARY_DIR}/${PROJECT_NAME}.map
    -Wl,--gc-sections
    --specs=nano.specs
    --specs=nosys.specs
)

add_custom_command(TARGET ${PROJECT_NAME}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${PROJECT_NAME}.elf> ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${PROJECT_NAME}.elf>
    BYPRODUCTS
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.hex
        ${CMAKE_BINARY_DIR}/${PROJECT_NAME}.bin
    COMMENT "Generating HEX/BIN and printing size"
)

