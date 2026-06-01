# STM32_FreeRTOS

Plain STM32F407VET6 project template using CMake, arm-none-eabi-gcc, Ninja, OpenOCD, STM32 HAL, and native FreeRTOS API.

## Board Assumptions

- MCU: STM32F407VET6
- Flash: 512 KB
- SRAM: 128 KB
- HSE: 8 MHz
- System clock: 168 MHz
- LED: PB2, active high
- FreeRTOS port: `Source/portable/GCC/ARM_CM3`
- FreeRTOS heap: `Source/portable/MemMang/heap_4.c`

## Layout

- `app/baremetal_blink`: HAL bare-metal blink, useful for first LED validation.
- `app/freertos_blink`: native FreeRTOS task blink, default build target.
- `bsp`: board clock and LED driver.
- `config`: HAL and FreeRTOS configuration.
- `startup`: GCC startup file.
- `linker`: STM32F407VET6 linker script.
- `third_party`: minimal STM32CubeF4 and FreeRTOS source files required by this template.

## Environment Check

```powershell
.\scripts\check_env.ps1
```

or:

```bat
scripts\check_env.cmd
```

## Build FreeRTOS Blink

```powershell
cmake -S . -B build -G Ninja -DAPP=freertos_blink
cmake --build build
```

Outputs:

- `build/STM32_FreeRTOS.elf`
- `build/STM32_FreeRTOS.hex`
- `build/STM32_FreeRTOS.bin`
- `build/STM32_FreeRTOS.map`

## Build Bare-Metal Blink First

```powershell
cmake -S . -B build -G Ninja -DAPP=baremetal_blink
cmake --build build
```

## Flash

The VS Code flash task and `scripts/flash.ps1` use CMSIS-DAP by default:

```powershell
.\scripts\flash.ps1
```

If you use ST-Link, change `interface/cmsis-dap.cfg` to `interface/stlink.cfg` in `.vscode/tasks.json` and `scripts/flash.ps1`.