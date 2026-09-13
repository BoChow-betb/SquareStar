[CmdletBinding()]
param(
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
    [bool]$Strict = $false,
    [bool]$BuildTests = $false,
    [switch]$Install,
    [string]$InstallDirectory = '',
    [switch]$Run
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot

function Require-Command([string]$Name, [string]$Hint) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) { throw "$Name was not found in PATH. $Hint" }
    return $command.Source
}

function Get-PeCoffSymbolTableInfo([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::ReadWrite)
    try {
        if ($stream.Length -lt 64) { return $null }
        $reader = [IO.BinaryReader]::new($stream)
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        if (($peOffset + 24) -gt $stream.Length) { return $null }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550) { return $null }

        # IMAGE_FILE_HEADER begins immediately after the PE signature.
        $stream.Position = $peOffset + 12
        $symbolTableOffset = [uint64]$reader.ReadUInt32()
        $symbolCount = [uint64]$reader.ReadUInt32()
        if ($symbolTableOffset -eq 0 -or $symbolCount -eq 0) {
            return [pscustomobject]@{ Bytes = [uint64]0; Symbols = [uint64]0 }
        }

        $symbolBytes = $symbolCount * 18
        $stringTableOffset = $symbolTableOffset + $symbolBytes
        $stringBytes = [uint64]0
        if (($stringTableOffset + 4) -le [uint64]$stream.Length) {
            $stream.Position = [int64]$stringTableOffset
            $declaredStringBytes = [uint64]$reader.ReadUInt32()
            if ($declaredStringBytes -ge 4 -and
                ($stringTableOffset + $declaredStringBytes) -le [uint64]$stream.Length) {
                $stringBytes = $declaredStringBytes
            }
        }
        return [pscustomobject]@{
            Bytes = $symbolBytes + $stringBytes
            Symbols = $symbolCount
        }
    }
    finally {
        $stream.Dispose()
    }
}

$metadataModeCount = @(
    @($BalancedSize, $StripDebug, $StripBinary) | Where-Object { [bool]$_ }
)
if ($metadataModeCount.Count -gt 1) {
    throw '-BalancedSize, -StripDebug, and -StripBinary are mutually exclusive.'
}
if ($Small) {
    if ($PSBoundParameters.ContainsKey('Optimization') -and $Optimization -ne 'Os') {
        throw '-Small is an alias for -Optimization Os and cannot be combined with another optimization level.'
    }
    $Optimization = 'Os'
}
if ($BalancedSize) {
    if ($PSBoundParameters.ContainsKey('Optimization') -and $Optimization -ne 'Os') {
        throw '-BalancedSize requires -Optimization Os (or omit -Optimization).'
    }
    $Optimization = 'Os'
}

$cmake = Require-Command 'cmake' 'Install CMake 3.24 or newer.'
$gxx = Require-Command 'g++' 'Install 64-bit MinGW-w64 and reopen the terminal.'
$gcc = Require-Command 'gcc' 'Install 64-bit MinGW-w64 and reopen the terminal.'
$ninja = Get-Command 'ninja' -ErrorAction SilentlyContinue
$generator = if ($ninja) { 'Ninja' } else { 'MinGW Makefiles' }

$flavor = if ($Install) {
    'release'
} elseif ($BuildTests) {
    'test'
} elseif ($FastBuild) {
    'dev'
} else {
    'release'
}
$buildRoot = Join-Path $projectRoot "build\$flavor-mingw"
$cacheFile = Join-Path $buildRoot 'CMakeCache.txt'
$configureStateFile = Join-Path $buildRoot 'squarestar-mingw-config-state.txt'
if ($Clean -and (Test-Path -LiteralPath $buildRoot)) {
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $projectRoot 'build'))
    $resolvedBuild = [IO.Path]::GetFullPath($buildRoot)
    if (-not $resolvedBuild.StartsWith(
            $expectedParent + [IO.Path]::DirectorySeparatorChar,
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove an unsafe build path: $resolvedBuild"
    }
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$cacheLauncher = ''
if ($CompilerCache -ne 'Off') {
    foreach ($candidate in @('sccache', 'ccache')) {
        $found = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($found) {
            $cacheLauncher = $found.Source
            break
        }
    }
    if ($CompilerCache -eq 'On' -and -not $cacheLauncher) {
        throw '-CompilerCache On was requested, but neither sccache nor ccache was found.'
    }
}

$strictValue = if ($Strict) { 'ON' } else { 'OFF' }
$testsValue = if ($BuildTests) { 'ON' } else { 'OFF' }
$ltoValue = if ($Lto) { 'ON' } else { 'OFF' }
$balancedSizeValue = if ($BalancedSize) { 'ON' } else { 'OFF' }
$stripDebugValue = if ($StripDebug) { 'ON' } else { 'OFF' }
$stripValue = if ($StripBinary) { 'ON' } else { 'OFF' }
$ninjaPath = if ($ninja) { [string]$ninja.Source } else { '' }

function Get-MingwConfigureState {
    # Keep the explicit configure step only for inputs that can change the
    # generated build graph or toolchain. Source globs use CONFIGURE_DEPENDS,
    # so cmake --build can still trigger CMake automatically when files move.
    $cmakeInputs = @(
        Get-Item -LiteralPath (Join-Path $projectRoot 'CMakeLists.txt')
        Get-ChildItem -LiteralPath (Join-Path $projectRoot 'cmake') -File -Recurse -Filter '*.cmake'
        Get-ChildItem -LiteralPath (Join-Path $projectRoot 'tests') -File -Recurse -Filter 'CMakeLists.txt'
    ) | Sort-Object -Property FullName -Unique
    $inputs = @()
    foreach ($inputFile in $cmakeInputs) {
        $relativePath = $inputFile.FullName.Substring($projectRoot.Length) -replace '^[\\/]+', ''
        $inputHash = (Get-FileHash -LiteralPath $inputFile.FullName -Algorithm SHA256).Hash
        $inputs += "CMakeInput|$relativePath|$inputHash"
    }
    $inputs += "Generator|$generator"
    $inputs += "MakeProgram|$ninjaPath"
    $inputs += "CCompiler|$gcc"
    $inputs += "CxxCompiler|$gxx"
    $inputs += "CompilerCache|$cacheLauncher"
    $inputs += "BuildTests|$testsValue"
    $inputs += "StrictWarnings|$strictValue"
    $inputs += "Optimization|$Optimization"
    $inputs += "Lto|$ltoValue"
    $inputs += "BalancedSize|$balancedSizeValue"
    $inputs += "StripDebug|$stripDebugValue"
    $inputs += "StripBinary|$stripValue"
    return [string]::Join("`n", $inputs)
}

$configureState = Get-MingwConfigureState
$cachedConfigureState = if (Test-Path -LiteralPath $configureStateFile -PathType Leaf) {
    [IO.File]::ReadAllText($configureStateFile)
} else {
    ''
}
$cachedGenerator = ''
if (Test-Path -LiteralPath $cacheFile -PathType Leaf) {
    $generatorLine = Get-Content -LiteralPath $cacheFile |
        Where-Object { $_ -like 'CMAKE_GENERATOR:INTERNAL=*' } |
        Select-Object -First 1
    if ($generatorLine) {
        $cachedGenerator = $generatorLine.Substring('CMAKE_GENERATOR:INTERNAL='.Length)
    }
}
$generatorChanged = -not [string]::IsNullOrWhiteSpace($cachedGenerator) -and
                    -not $cachedGenerator.Equals($generator, [StringComparison]::Ordinal)
$needsConfigure = $Reconfigure -or
                  -not (Test-Path -LiteralPath $cacheFile -PathType Leaf) -or
                  -not $cachedConfigureState.Equals($configureState, [StringComparison]::Ordinal)

if ($needsConfigure) {
    $configure = @(
        '-S', $projectRoot,
        '-B', $buildRoot,
        '-G', $generator,
        '-DCMAKE_BUILD_TYPE=Release',
        "-DCMAKE_C_COMPILER=$gcc",
        "-DCMAKE_CXX_COMPILER=$gxx",
        "-DSQUARESTAR_BUILD_TESTS=$testsValue",
        "-DSQUARESTAR_STRICT_WARNINGS=$strictValue",
        "-DSQUARESTAR_OPTIMIZATION=$Optimization",
        "-DSQUARESTAR_ENABLE_LTO=$ltoValue",
        "-DSQUARESTAR_BALANCED_SIZE=$balancedSizeValue",
        "-DSQUARESTAR_STRIP_DEBUG=$stripDebugValue",
        "-DSQUARESTAR_STRIP_BINARY=$stripValue"
    )
    if ($cacheLauncher) {
        $configure += "-DCMAKE_C_COMPILER_LAUNCHER=$cacheLauncher"
        $configure += "-DCMAKE_CXX_COMPILER_LAUNCHER=$cacheLauncher"
    }
    if ($Reconfigure -or $generatorChanged -or
        -not (Test-Path -LiteralPath $cacheFile -PathType Leaf)) {
        $configure += '--fresh'
    }

    Write-Host "Configuring SquareStar MinGW $flavor tree with $generator..."
    & $cmake @configure
    if ($LASTEXITCODE -ne 0) { throw "MinGW CMake configuration failed with exit code $LASTEXITCODE." }
    [IO.File]::WriteAllText($configureStateFile, $configureState)
} else {
    Write-Host "Reusing the existing MinGW $flavor CMake configuration."
}

$buildArguments = @('--build', $buildRoot, '--parallel', $Jobs)
if ($FastBuild -and -not $BuildTests) { $buildArguments += @('--target', 'SquareStar') }
& $cmake @buildArguments
if ($LASTEXITCODE -ne 0) { throw "MinGW build failed with exit code $LASTEXITCODE." }

if ($BuildTests) {
    & $cmake --build $buildRoot --target test
    if ($LASTEXITCODE -ne 0) { throw "MinGW tests failed with exit code $LASTEXITCODE." }
}

$executable = Join-Path $buildRoot 'SquareStar.exe'
if ($Install) {
    if (-not $InstallDirectory) { $InstallDirectory = Join-Path $projectRoot 'prebuilt-mingw' }
    & $cmake --install $buildRoot --prefix $InstallDirectory
    if ($LASTEXITCODE -ne 0) { throw "MinGW install failed with exit code $LASTEXITCODE." }
    $executable = Join-Path $InstallDirectory 'SquareStar.exe'
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "The MinGW build completed without producing $executable."
}
$metadataMode = if ($BalancedSize) {
    'balanced COFF trim (--discard-all; globals retained)'
} elseif ($StripDebug) {
    'debug-only strip (--strip-debug)'
} elseif ($StripBinary) {
    'full strip (-s; explicit)'
} else {
    'full strip (-s; Release default)'
}
$exeBytes = (Get-Item -LiteralPath $executable).Length
$exeMiB = $exeBytes / 1MB
Write-Host ("MinGW build complete: {0} ({1:N2} MiB; {2})" -f $executable, $exeMiB, $metadataMode)
$coffInfo = Get-PeCoffSymbolTableInfo $executable
if ($coffInfo) {
    Write-Host ("COFF symbol/string table: {0:N2} MiB ({1:N0} symbols)" -f ($coffInfo.Bytes / 1MB), $coffInfo.Symbols)
}
if ($Run) { & $executable }
