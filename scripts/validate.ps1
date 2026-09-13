[CmdletBinding()]
param(
    [ValidateSet('Auto', 'MSVC', 'MinGW', 'Both')]
    [string]$Toolchain = 'Auto',
    [ValidateRange(1, 256)]
    [int]$Jobs = [Environment]::ProcessorCount,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildScript = Join-Path $projectRoot 'build.ps1'

$minimumVisualStudioMajor = 17
$minimumVisualStudioVersionRange = "[$($minimumVisualStudioMajor).0,)"

function Get-ActiveVisualStudioDeveloperShellMajor {
    if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
        return $null
    }
    $versionText = [string]$env:VisualStudioVersion
    if ($versionText -match '^\s*(\d+)(?:\.|$)') {
        $major = [int]$Matches[1]
        if ($major -ge $minimumVisualStudioMajor) {
            return $major
        }
    }
    return $null
}

function Test-MsvcAvailable {
    if ($null -ne (Get-ActiveVisualStudioDeveloperShellMajor)) { return $true }

    $programFilesX86 = ${env:ProgramFiles(x86)}
    if ([string]::IsNullOrWhiteSpace($programFilesX86)) { return $false }
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) { return $false }
    $installation = @(& $vswhere `
        -latest `
        -prerelease `
        -products '*' `
        -version $minimumVisualStudioVersionRange `
        -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
        -property installationPath 2>$null) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Select-Object -First 1
    return [bool]$installation
}

function Test-MingwAvailable {
    if (Get-Command g++.exe -ErrorAction SilentlyContinue) { return $true }
    if (Get-Command g++ -ErrorAction SilentlyContinue) { return $true }
    return $false
}

$msvcAvailable = Test-MsvcAvailable
$mingwAvailable = Test-MingwAvailable
$msvcDeveloperShell = $msvcAvailable -and ($null -ne (Get-ActiveVisualStudioDeveloperShellMajor))

$selected = @()
switch ($Toolchain) {
    'MSVC' {
        if (-not $msvcAvailable) {
            throw 'MSVC validation was requested, but Visual Studio C++ Build Tools were not detected.'
        }
        $selected = @('MSVC')
    }
    'MinGW' {
        if (-not $mingwAvailable) {
            throw 'MinGW validation was requested, but g++ was not detected in PATH.'
        }
        $selected = @('MinGW')
    }
    'Both' {
        if (-not $msvcAvailable -or -not $mingwAvailable) {
            throw '-Toolchain Both requires both Visual Studio C++ Build Tools and MinGW g++.'
        }
        $selected = @('MSVC', 'MinGW')
    }
    default {
        if ($msvcDeveloperShell) {
            $selected = @('MSVC')
        } elseif ($mingwAvailable) {
            $selected = @('MinGW')
        } elseif ($msvcAvailable) {
            $selected = @('MSVC')
        } else {
            throw "No supported Windows C++ toolchain was detected. Install Visual Studio $minimumVisualStudioMajor.x or newer C++ Build Tools, or 64-bit MinGW-w64."
        }
    }
}

Write-Host '============================================================'
Write-Host 'SquareStar contributor validation'
Write-Host ('Toolchain(s): {0}' -f ($selected -join ', '))
Write-Host 'Checks: full Windows compile + strict warnings + tests'
Write-Host '============================================================'

$timer = [Diagnostics.Stopwatch]::StartNew()
for ($index = 0; $index -lt $selected.Count; ++$index) {
    $current = $selected[$index]
    Write-Host ''
    Write-Host ('=> Validating {0}...' -f $current)
    $parameters = @{
        Toolchain = $current
        Jobs = $Jobs
        Test = $true
        Strict = $true
        FastBuild = $true
    }
    if ($Clean) {
        $parameters['Clean'] = $true
    }

    & $buildScript @parameters
    if ($LASTEXITCODE -ne 0) {
        throw "SquareStar contributor validation failed for $current with exit code $LASTEXITCODE."
    }
}

$timer.Stop()

Write-Host ''
Write-Host '============================================================'
Write-Host ('Validation passed in {0:N1}s.' -f $timer.Elapsed.TotalSeconds)
Write-Host 'Safe to push: the selected Windows toolchain(s) compiled the real shell and all tests passed.'
Write-Host '============================================================'
