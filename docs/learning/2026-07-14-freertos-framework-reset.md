# FreeRTOS framework reset

## Goal

Remove the previous multi-sensor application code and keep a clean FreeRTOS
framework for a new project.

## Changes

- Replaced `app/freertos_blink/main.c` with a minimal FreeRTOS application.
- Removed build references to DHT11, ST7735S, MPU6050, and VL53L0X drivers.
- Removed previous sensor task/message files from `app/freertos_blink`.
- Kept `app/baremetal_blink/main.c` as the bare-metal test example.
- Kept board startup, linker, FreeRTOS, HAL, UART debug, and interrupt support.

## Current Runtime Path

```text
Reset_Handler
  -> main
  -> Board_Init
  -> UART init
  -> create UART mutex
  -> create HEART and WORKER tasks
  -> vTaskStartScheduler
```

## Verification

Normal test performed:

```powershell
cmake -S . -B build -G Ninja -DAPP=freertos_blink
cmake --build build
```

Result:

- Build passed.
- Firmware output generated at `build/STM32_FreeRTOS.elf`.

## Not Yet Verified On Hardware

- UART boot messages after flashing.
- HEART task 1-second periodic output.
- WORKER task 3-second periodic output.
- Heap stability over long runtime.

## Suggested Fault Tests

1. Open UART at 115200 8N1 and press reset. Expect boot messages.
2. Disconnect the USB-UART RX line from PA9. Expect firmware to keep running.
3. Temporarily lower `configTOTAL_HEAP_SIZE` and rebuild. Expect malloc failure
   hook if task creation cannot allocate memory.

## Interview Follow-Ups

1. Why is `vTaskStartScheduler()` the point where FreeRTOS takes control?
2. Why does UART output use a mutex even in a small project?
3. What does `vTaskDelayUntil()` guarantee compared with `vTaskDelay()`?
4. What evidence proves the scheduler is running?
5. Why is compilation necessary but not enough for embedded validation?
