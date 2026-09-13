[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('fast', 'validate', 'release', 'clean', 'help', 'Auto', 'MSVC', 'MinGW')]
    [string]$Command,

    [ValidateSet('Auto', 'MSVC', 'MinGW', 'Both')]
    [string]$Toolchain,

    [ValidateRange(1, 256)]
    [int]$Jobs,

    [switch]$Clean,
    [switch]$Reconfigure,
    [switch]$FastBuild,

    [ValidateSet('O1', 'O2', 'O3', 'Os')]
    [string]$Optimization,

    [ValidateSet('Auto', 'On', 'Off')]
    [string]$CompilerCache,

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

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $projectRoot 'build.ps1'
$validateScript = Join-Path $PSScriptRoot 'validate.ps1'
$cleanScript = Join-Path $PSScriptRoot 'clear-build-data.ps1'

function Show-BuildHelp {
    @'
SquareStar build commands

Usage:
  build.cmd                     Open the interactive build menu.
  build.cmd fast [build options]
  build.cmd validate [validation options]
  build.cmd release [build options]
  build.cmd clean
  build.cmd help
  build.cmd [build options]

Commands:
  fast            Incremental developer build. Release/NDEBUG, O2 by default.
                  FastBuild changes the build workflow only; it does NOT lower
                  the compiler optimization level.
  validate        Strict compile + C++ tests.
  release         Strict, test-gated portable package. O2 by default.
  clean           Remove generated project build/package/cache data.
  help            Show this help.

Build options:
  -Toolchain Auto|MSVC|MinGW
                  Default: Auto. Auto prefers an active VS developer shell,
                  otherwise MinGW on PATH, otherwise installed supported Visual Studio C++ tools.
  -Jobs N         Parallel jobs, 1..256. Default: logical processor count.
  -Optimization O1|O2|O3|Os
                  Default: O2.
  -CompilerCache Auto|On|Off
                  Default: Auto. "On" requires sccache/ccache on MinGW.

  -Clean          Clean the selected build tree before building.
  -Reconfigure    Force a fresh CMake configure.
  -FastBuild      Use the dev build tree, build only SquareStar when possible,
                  and skip install. It does not change optimization.
  -Lto            Enable LTO when supported.
  -Strict         Enable strict first-party warnings.
  -Test           Build and run tests.
  -Package        Create the portable release package (also forces tests).
  -Run            Start SquareStar after a successful build.

MinGW-only size/metadata options:
  -Small          Alias for -Optimization Os.
  -BalancedSize   Requires Os; trims COFF metadata while retaining globals.
  -StripDebug     Strip debug-only COFF information.
  -StripBinary    Explicitly select full strip (-s); this is already the MinGW Release default.
                  BalancedSize, StripDebug, and StripBinary are mutually exclusive.

Validation options:
  -Toolchain Auto|MSVC|MinGW|Both
  -Jobs N         1..256.
  -Clean

Examples:
  build.cmd
  build.cmd fast -Toolchain MinGW -Jobs 8
  build.cmd fast -Optimization O3
  build.cmd validate -Toolchain Both -Clean
  build.cmd release
  build.cmd -Toolchain MSVC -Test -Strict -Run

Notes:
  "build.cmd" with no arguments opens the build menu.
  Use "build.cmd fast ..." for a direct developer build.
  Build options passed without a command are forwarded to build.ps1.
'@ | Write-Host
}

function Get-ForwardedBuildParameters {
    $result = @{}
    foreach ($entry in $PSBoundParameters.GetEnumerator()) {
        if ($entry.Key -ne 'Command') {
            $result[$entry.Key] = $entry.Value
        }
    }
    return $result
}

function Assert-BuildToolchain {
    param([hashtable]$Parameters)
    if ($Parameters.ContainsKey('Toolchain') -and $Parameters['Toolchain'] -eq 'Both') {
        throw '-Toolchain Both is accepted only by "build.cmd validate". For a normal build choose Auto, MSVC, or MinGW.'
    }
}

$hasCommand = $PSBoundParameters.ContainsKey('Command')
$hasAnyOption = @($PSBoundParameters.Keys | Where-Object { $_ -ne 'Command' }).Count -gt 0

# Preserve the old convenient positional form: build.cmd MinGW
if ($hasCommand -and $Command -in @('Auto', 'MSVC', 'MinGW')) {
    if ($PSBoundParameters.ContainsKey('Toolchain')) {
        throw 'Specify the toolchain either positionally or with -Toolchain, not both.'
    }
    $parameters = Get-ForwardedBuildParameters
    $parameters['Toolchain'] = $Command
    & $buildScript @parameters
    if (-not $?) { exit 1 }
    exit 0
}

# No arguments = normal fast developer build.
if (-not $hasCommand -and -not $hasAnyOption) {
    & $buildScript -FastBuild
    if (-not $?) { exit 1 }
    exit 0
}

# Named options without a command = direct advanced build.ps1 mode.
if (-not $hasCommand) {
    $parameters = Get-ForwardedBuildParameters
    Assert-BuildToolchain $parameters
    & $buildScript @parameters
    if (-not $?) { exit 1 }
    exit 0
}

switch ($Command) {
    'fast' {
        $parameters = Get-ForwardedBuildParameters
        Assert-BuildToolchain $parameters
        $parameters['FastBuild'] = $true
        & $buildScript @parameters
        if (-not $?) { exit 1 }
        exit 0
    }

    'release' {
        $parameters = Get-ForwardedBuildParameters
        Assert-BuildToolchain $parameters
        $parameters['Test'] = $true
        $parameters['Strict'] = $true
        $parameters['Package'] = $true
        & $buildScript @parameters
        if (-not $?) { exit 1 }
        exit 0
    }

    'validate' {
        $allowed = @('Command', 'Toolchain', 'Jobs', 'Clean')
        $unsupported = @($PSBoundParameters.Keys | Where-Object { $_ -notin $allowed })
        if ($unsupported.Count -gt 0) {
            throw ('"build.cmd validate" accepts only -Toolchain, -Jobs, and -Clean. Unsupported: ' +
                   ($unsupported -join ', '))
        }

        $parameters = @{}
        foreach ($name in @('Toolchain', 'Jobs', 'Clean')) {
            if ($PSBoundParameters.ContainsKey($name)) {
                $parameters[$name] = $PSBoundParameters[$name]
            }
        }
        & $validateScript @parameters
        if (-not $?) { exit 1 }
        exit 0
    }

    'clean' {
        if ($hasAnyOption) {
            throw '"build.cmd clean" does not accept additional options.'
        }
        & $cleanScript
        if (-not $?) { exit 1 }
        exit 0
    }

    'help' {
        if ($hasAnyOption) {
            throw '"build.cmd help" does not accept additional options.'
        }
        Show-BuildHelp
        exit 0
    }
}
