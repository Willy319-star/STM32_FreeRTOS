param(
    [ValidateSet("STM32F407_FreeRTOS_multisensors", "baremetal_blink")]
    [string]$App = "STM32F407_FreeRTOS_multisensors",
    [switch]$ConfigureOnly
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\toolchain_env.ps1"
$root = Split-Path -Parent $PSScriptRoot

cmake -S $root -B "$root\build" -G Ninja "-DAPP=$App"
if (!$ConfigureOnly) {
    cmake --build "$root\build"
}
