param(
    [ValidateSet("freertos_blink", "baremetal_blink")]
    [string]$App = "freertos_blink",
    [switch]$ConfigureOnly
)

$ErrorActionPreference = "Stop"
. "$PSScriptRoot\toolchain_env.ps1"
$root = Split-Path -Parent $PSScriptRoot

cmake -S $root -B "$root\build" -G Ninja "-DAPP=$App"
if (!$ConfigureOnly) {
    cmake --build "$root\build"
}
