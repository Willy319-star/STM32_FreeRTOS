$ErrorActionPreference = "Stop"
. "$PSScriptRoot\toolchain_env.ps1"
$root = Split-Path -Parent $PSScriptRoot
$elf = Join-Path $root "build\STM32_FreeRTOS.elf"

if (!(Test-Path $elf)) {
    throw "ELF not found: $elf. Build first."
}

$openocdElf = $elf -replace '\\', '/'
openocd -f interface/cmsis-dap.cfg -f target/stm32f4x.cfg -c "program {$openocdElf} verify" -c "reset run" -c "shutdown"
