[CmdletBinding()]
param(
    [ValidateSet('Auto', 'MSVC', 'MinGW')]
    [string]$Toolchain = 'Auto',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Clean,
    [switch]$Reconfigure,
    [switch]$FastBuild,
    [ValidateSet('O1', 'O2', 'O3', 'Os')]
    [string]$Optimization = 'O2',
    [ValidateSet('Auto', 'On', 'Off')]
    [string]$CompilerCache = 'Auto',
    [switch]$Small,
    [switch]$Lto,
    [switch]$BalancedSize,
    [switch]$StripDebug,
    [switch]$StripBinary,
    [switch]$Strict,
    [switch]$Test,
    [switch]$Package,
    [switch]$Run
)

# Stable public entry point. A named splat preserves PowerShell parameter
# binding; forwarding the raw string array would make "-Toolchain" positional.
& (Join-Path $PSScriptRoot 'scripts\build-single-file.ps1') @PSBoundParameters
