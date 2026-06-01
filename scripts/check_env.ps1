$ErrorActionPreference = "Stop"
. "$PSScriptRoot\toolchain_env.ps1"

$tools = @(
    "arm-none-eabi-gcc",
    "cmake",
    "ninja",
    "openocd"
)

$missing = @()

foreach ($tool in $tools) {
    $cmd = Get-Command $tool -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        Write-Host "[MISS] $tool"
        $missing += $tool
    } else {
        Write-Host "[ OK ] $tool -> $($cmd.Source)"
    }
}

if ($missing.Count -gt 0) {
    Write-Error "Missing tools: $($missing -join ', ')"
    exit 1
}

Write-Host "Environment looks good."
