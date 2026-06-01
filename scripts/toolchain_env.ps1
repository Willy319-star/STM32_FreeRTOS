function Add-PathIfExists {
    param([string]$Path)

    if ((Test-Path $Path) -and (($env:PATH -split ';') -notcontains $Path)) {
        $env:PATH = "$Path;$env:PATH"
    }
}

Add-PathIfExists "C:\Users\EDY\tools\stm32\gcc\bin"
Add-PathIfExists "C:\Users\EDY\tools\stm32\openocd\xpack-openocd-0.12.0-7\bin"
