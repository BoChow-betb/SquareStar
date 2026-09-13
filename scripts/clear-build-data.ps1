[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$pathTrimChars = [char[]]@(
    [IO.Path]::DirectorySeparatorChar,
    [IO.Path]::AltDirectorySeparatorChar)
$projectPrefix = $projectRoot.TrimEnd($pathTrimChars) +
    [IO.Path]::DirectorySeparatorChar

function Remove-SquareStarProjectPath([string]$RelativePath) {
    $target = [IO.Path]::GetFullPath((Join-Path $projectRoot $RelativePath))
    if (-not $target.StartsWith($projectPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove an unsafe project path: $target"
    }
    if (-not (Test-Path -LiteralPath $target)) {
        Write-Host "Already clean: $target"
        return
    }
    Write-Host "Removing: $target"
    Remove-Item -LiteralPath $target -Recurse -Force
}

function Remove-SquareStarLocalBuildCache {
    if (-not $env:LOCALAPPDATA) {
        return
    }
    $squareStarLocalRoot = [IO.Path]::GetFullPath(
        (Join-Path $env:LOCALAPPDATA 'SquareStar'))
    $squareStarLocalPrefix = $squareStarLocalRoot.TrimEnd($pathTrimChars) +
        [IO.Path]::DirectorySeparatorChar
    $buildCache = [IO.Path]::GetFullPath(
        (Join-Path $squareStarLocalRoot 'build-cache'))
    if (-not $buildCache.StartsWith(
            $squareStarLocalPrefix,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove an unsafe local build-cache path: $buildCache"
    }
    if (-not (Test-Path -LiteralPath $buildCache)) {
        Write-Host "Already clean: $buildCache"
        return
    }
    Write-Host "Removing: $buildCache"
    Remove-Item -LiteralPath $buildCache -Recurse -Force
}

Write-Host 'Clearing SquareStar generated build data...'
foreach ($relativePath in @(
        'build',
        'dist',
        'prebuilt',
        'prebuilt-mingw',
        '.squarestar-cache')) {
    Remove-SquareStarProjectPath $relativePath
}

$compileCommands = Join-Path $projectRoot 'compile_commands.json'
if (Test-Path -LiteralPath $compileCommands -PathType Leaf) {
    Write-Host "Removing: $compileCommands"
    Remove-Item -LiteralPath $compileCommands -Force
}

# This cache is SquareStar-owned and contains CMake/FetchContent dependency
# material. Deliberately do not clear global sccache/ccache storage because it
# can be shared by unrelated projects.
Remove-SquareStarLocalBuildCache

Write-Host ''
Write-Host 'SquareStar build cache and generated build data cleared.'
Write-Host 'Application settings, credentials, and other runtime user data were not touched.'
