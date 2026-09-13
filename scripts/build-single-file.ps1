[CmdletBinding()]
param(
    [Parameter(Position = 0)]
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

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeProjectText = [IO.File]::ReadAllText((Join-Path $projectRoot 'CMakeLists.txt'))
$projectVersionMatch = [regex]::Match(
    $cmakeProjectText,
    'project\(SquareStar\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)')
if (-not $projectVersionMatch.Success) {
    throw 'Could not read the SquareStar semantic version from CMakeLists.txt.'
}
$projectVersion = $projectVersionMatch.Groups[1].Value
# Publishing is a release gate, not merely an install mode. A portable artifact
# must never be created from a build that skipped the full test suite.
if ($Package) {
    $Test = $true
}
$strictWarnings = [bool]($Strict -or $Package)
$overallBuildTimer = [Diagnostics.Stopwatch]::StartNew()
$buildCacheRoot = if ($env:LOCALAPPDATA) {
    Join-Path $env:LOCALAPPDATA 'SquareStar\build-cache'
} else {
    Join-Path $projectRoot '.squarestar-cache\build-cache'
}
$dependencyCacheRoot = Join-Path $buildCacheRoot 'fetchcontent-msvc-x64'
New-Item -ItemType Directory -Force -Path $dependencyCacheRoot | Out-Null

function Require-Command([string]$Name, [string]$InstallHint) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "$Name was not found in PATH. $InstallHint"
    }
}

$minimumVisualStudioMajor = 17
$minimumVisualStudioVersionRange = "[$($minimumVisualStudioMajor).0,)"

function Get-ActiveVisualStudioDeveloperShellMajor {
    if (-not (Get-Command 'cl.exe' -ErrorAction SilentlyContinue)) {
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

function Normalize-VisualStudioInstancePath([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) {
        return ''
    }

    $fullPath = [IO.Path]::GetFullPath($Path)
    # VSINSTALLDIR commonly ends in '\'. When PowerShell passes a quoted native
    # argument that ends in a backslash, that slash can escape the closing quote
    # and make every following CMake option part of CMAKE_GENERATOR_INSTANCE.
    # Never pass a generator-instance path with a trailing directory separator.
    return $fullPath.TrimEnd([char[]]@('\', '/'))
}

function Get-ActiveVisualStudioInstallPath {
    $path = [string]$env:VSINSTALLDIR
    if ([string]::IsNullOrWhiteSpace($path)) {
        return $null
    }
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        return $null
    }
    return Normalize-VisualStudioInstancePath $path
}

function Test-ActiveMsvcX64Environment {
    $targetArch = [string]$env:VSCMD_ARG_TGT_ARCH
    if ($targetArch -notin @('x64', 'amd64')) {
        return $false
    }

    $compiler = Get-Command 'cl.exe' -ErrorAction SilentlyContinue
    $linker = Get-Command 'link.exe' -ErrorAction SilentlyContinue
    return [bool]($compiler -and $linker)
}

function Import-VisualStudioX64Environment([string]$VisualStudioPath) {
    if ([string]::IsNullOrWhiteSpace($VisualStudioPath)) {
        return $false
    }

    $visualStudioPath = Normalize-VisualStudioInstancePath $VisualStudioPath

    # Prefer Visual Studio's supported Developer PowerShell initializer. It
    # updates this process directly and can switch an x86 developer shell to
    # an amd64 host/target without fragile cmd.exe /c quoting.
    $launchVsDevShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
    if (Test-Path -LiteralPath $launchVsDevShell -PathType Leaf) {
        try {
            & $launchVsDevShell `
                -Arch amd64 `
                -HostArch amd64 `
                -SkipAutomaticLocation *> $null
            if (Test-ActiveMsvcX64Environment) {
                return $true
            }
        }
        catch {
            Write-Verbose (
                'Launch-VsDevShell.ps1 could not initialize amd64; trying vcvars64.bat. {0}' -f
                $_.Exception.Message)
        }
    }

    # Fallback for installations where Developer PowerShell cannot be entered.
    # Put the vcvars call in a temporary batch file so paths with spaces never
    # have to be nested inside cmd.exe /c quoting. Import the final environment
    # printed by that child process into this PowerShell process.
    $vcvars64 = Join-Path $visualStudioPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path -LiteralPath $vcvars64 -PathType Leaf)) {
        return $false
    }

    $wrapperPath = Join-Path (
        [IO.Path]::GetTempPath()) (
        'squarestar-vcvars-{0}.cmd' -f [Guid]::NewGuid().ToString('N'))
    try {
        $wrapperLines = @(
            '@echo off',
            ('call "{0}" >nul' -f $vcvars64),
            'if errorlevel 1 exit /b %errorlevel%',
            'set'
        )
        [IO.File]::WriteAllLines(
            $wrapperPath,
            $wrapperLines,
            [Text.Encoding]::Default)

        $environmentLines = @(& $wrapperPath 2>$null)
        $vcvarsExitCode = $LASTEXITCODE
        if ($vcvarsExitCode -ne 0 -or $environmentLines.Count -eq 0) {
            return $false
        }

        foreach ($line in $environmentLines) {
            $lineText = [string]$line
            $separator = $lineText.IndexOf('=')
            # cmd.exe also exposes pseudo variables such as '=C:'. Skip those.
            if ($separator -le 0) {
                continue
            }
            $name = $lineText.Substring(0, $separator)
            $value = $lineText.Substring($separator + 1)
            [Environment]::SetEnvironmentVariable(
                $name,
                $value,
                [EnvironmentVariableTarget]::Process)
        }

        return (Test-ActiveMsvcX64Environment)
    }
    catch {
        Write-Verbose (
            'vcvars64.bat fallback could not initialize amd64. {0}' -f
            $_.Exception.Message)
        return $false
    }
    finally {
        Remove-Item -LiteralPath $wrapperPath -Force -ErrorAction SilentlyContinue
    }
}

function Get-ActiveMsvcShellGenerator {
    if ($null -eq (Get-ActiveVisualStudioDeveloperShellMajor)) {
        return $null
    }

    $targetArch = [string]$env:VSCMD_ARG_TGT_ARCH
    if ([string]::IsNullOrWhiteSpace($targetArch) -or
        $targetArch -notin @('x64', 'amd64')) {
        $vsInstallPath = Get-ActiveVisualStudioInstallPath
        if ([string]::IsNullOrWhiteSpace($vsInstallPath)) {
            return $null
        }
        Write-Host (
            'Reinitializing the active Visual Studio developer environment for x64: {0}' -f
            $vsInstallPath)
        if (-not (Import-VisualStudioX64Environment $vsInstallPath)) {
            return $null
        }
    }

    $compiler = Get-Command 'cl.exe' -ErrorAction SilentlyContinue
    if (-not $compiler) {
        return $null
    }

    $compilerPath = [string]$compiler.Source
    $compilerDirectory = Split-Path -Parent $compilerPath
    $resourceCompiler = Get-Command 'rc.exe' -ErrorAction SilentlyContinue
    if (-not $resourceCompiler) {
        throw 'The x64 MSVC compiler is available, but rc.exe is missing. SquareStar embeds Windows resources and requires a Windows SDK. Add a Windows 10/11 SDK component to the Visual Studio Desktop development with C++ workload, then rerun the same build command.'
    }
    $resourceCompilerPath = [string]$resourceCompiler.Source
    $vsInstallPath = Get-ActiveVisualStudioInstallPath

    # First use Ninja already on PATH.
    $ninjaCommand = Get-Command 'ninja.exe' -ErrorAction SilentlyContinue
    if (-not $ninjaCommand) {
        $ninjaCommand = Get-Command 'ninja' -ErrorAction SilentlyContinue
    }
    if ($ninjaCommand) {
        return [pscustomobject]@{
            Generator = 'Ninja'
            MultiConfig = $false
            VisualStudioGenerator = $false
            Instance = ''
            CompilerPath = $compilerPath
            MakeProgram = [string]$ninjaCommand.Source
            ResourceCompilerPath = $resourceCompilerPath
            Source = "active Visual Studio $($env:VisualStudioVersion) developer shell (cl.exe + Ninja)"
        }
    }

    # Visual Studio's CMake component normally bundles Ninja even when it is not
    # added to PATH. Prefer it before falling back to a VS solution generator.
    if (-not [string]::IsNullOrWhiteSpace($vsInstallPath)) {
        $bundledNinja = Join-Path $vsInstallPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (Test-Path -LiteralPath $bundledNinja -PathType Leaf) {
            return [pscustomobject]@{
                Generator = 'Ninja'
                MultiConfig = $false
                VisualStudioGenerator = $false
                Instance = ''
                CompilerPath = $compilerPath
                MakeProgram = $bundledNinja
                ResourceCompilerPath = $resourceCompilerPath
                Source = "active Visual Studio $($env:VisualStudioVersion) developer shell (cl.exe + Visual Studio bundled Ninja)"
            }
        }
    }

    # nmake.exe ships beside cl.exe in a normal MSVC toolset. Looking beside the
    # selected compiler is more reliable than assuming PATH contains nmake.
    $nmakeCommand = Get-Command 'nmake.exe' -ErrorAction SilentlyContinue
    $nmakePath = if ($nmakeCommand) {
        [string]$nmakeCommand.Source
    } else {
        Join-Path $compilerDirectory 'nmake.exe'
    }

    if (Test-Path -LiteralPath $nmakePath -PathType Leaf) {
        return [pscustomobject]@{
            Generator = 'NMake Makefiles'
            MultiConfig = $false
            VisualStudioGenerator = $false
            Instance = ''
            CompilerPath = $compilerPath
            MakeProgram = $nmakePath
            ResourceCompilerPath = $resourceCompilerPath
            Source = "active Visual Studio $($env:VisualStudioVersion) developer shell (cl.exe + NMake)"
        }
    }

    return $null
}

$script:visualStudioCppInstallationsResolved = $false
$script:visualStudioCppInstallations = @()

function Get-VisualStudioCppInstallations {
    if ($script:visualStudioCppInstallationsResolved) {
        return @($script:visualStudioCppInstallations)
    }

    $script:visualStudioCppInstallationsResolved = $true
    $script:visualStudioCppInstallations = @()

    $programFilesX86 = ${env:ProgramFiles(x86)}
    if ([string]::IsNullOrWhiteSpace($programFilesX86)) {
        return @()
    }

    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
        return @()
    }

    $installationJson = @(& $vswhere `
        -all `
        -prerelease `
        -products '*' `
        -version $minimumVisualStudioVersionRange `
        -requires 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' `
        -format json `
        -utf8 2>$null)
    if ($LASTEXITCODE -ne 0 -or $installationJson.Count -eq 0) {
        return @()
    }

    try {
        $installations = @((($installationJson -join "`n") | ConvertFrom-Json))
    }
    catch {
        return @()
    }

    $result = @()
    foreach ($installation in $installations) {
        $versionText = [string]$installation.installationVersion
        if ($versionText -notmatch '^\s*(\d+)(?:\.|$)') {
            continue
        }
        $major = [int]$Matches[1]
        if ($major -lt $minimumVisualStudioMajor) {
            continue
        }
        $path = [string]$installation.installationPath
        if ([string]::IsNullOrWhiteSpace($path)) {
            continue
        }
        $path = Normalize-VisualStudioInstancePath $path
        $result += [pscustomobject]@{
            Major = $major
            Version = $versionText
            Path = $path
        }
    }

    $script:visualStudioCppInstallations = @(
        $result | Sort-Object -Property Major -Descending)
    return @($script:visualStudioCppInstallations)
}

function Get-CMakeVisualStudioGenerators {
    $capabilityJson = @(& cmake -E capabilities 2>$null)
    if ($LASTEXITCODE -ne 0 -or $capabilityJson.Count -eq 0) {
        throw 'CMake could not report its supported generators. Reinstall or update CMake.'
    }

    try {
        $capabilities = (($capabilityJson -join "`n") | ConvertFrom-Json)
    }
    catch {
        throw 'CMake returned invalid generator capability data. Reinstall or update CMake.'
    }

    $result = @()
    foreach ($generator in @($capabilities.generators)) {
        $name = [string]$generator.name
        if ($name -match '^Visual Studio (\d+)\s+') {
            $major = [int]$Matches[1]
            if ($major -ge $minimumVisualStudioMajor) {
                $result += [pscustomobject]@{
                    Major = $major
                    Name = $name
                }
            }
        }
    }
    return @($result | Sort-Object -Property Major -Descending)
}

function Resolve-MsvcCMakeGenerator {
    $activeMajor = Get-ActiveVisualStudioDeveloperShellMajor
    $activeShellGenerator = Get-ActiveMsvcShellGenerator
    if ($activeShellGenerator) {
        return $activeShellGenerator
    }

    $installations = @(Get-VisualStudioCppInstallations)
    $activeInstance = Get-ActiveVisualStudioInstallPath

    # A normal PowerShell does not inherit VC/SDK variables. Bootstrap the x64
    # environment from the detected VS installation and prefer cl.exe directly.
    # This also avoids VS 2026 generator/toolset auto-discovery regressions.
    $bootstrapPaths = @()
    if (-not [string]::IsNullOrWhiteSpace($activeInstance)) {
        $bootstrapPaths += $activeInstance
    }
    $bootstrapPaths += @($installations | ForEach-Object { [string]$_.Path })
    foreach ($bootstrapPath in @($bootstrapPaths | Where-Object {
        -not [string]::IsNullOrWhiteSpace($_)
    } | Select-Object -Unique)) {
        Write-Host (
            'Initializing the x64 MSVC command-line environment from Visual Studio: {0}' -f
            $bootstrapPath)
        if (Import-VisualStudioX64Environment $bootstrapPath) {
            $activeMajor = Get-ActiveVisualStudioDeveloperShellMajor
            $activeShellGenerator = Get-ActiveMsvcShellGenerator
            if ($activeShellGenerator) {
                return $activeShellGenerator
            }
        }
    }

    $detectedDirectBackendMajors = @()
    if ($null -ne $activeMajor) {
        $detectedDirectBackendMajors += $activeMajor
    }
    $detectedDirectBackendMajors += @($installations | ForEach-Object { $_.Major })
    if (@($detectedDirectBackendMajors | Where-Object { $_ -ge 18 }).Count -gt 0) {
        throw 'Visual Studio 18 or newer was detected, but SquareStar could not establish a usable x64 MSVC command-line environment. The build intentionally avoids falling back to the Visual Studio 18 CMake generator because that path can report an unknown C/C++ compiler. Verify that the Desktop development with C++ workload and a Windows SDK are installed.'
    }

    # Query CMake's Visual Studio generator list only after every direct
    # cl.exe backend has failed. Successful cl.exe + Ninja/NMake builds never
    # need to pay for a separate `cmake -E capabilities` process.
    $generators = @(Get-CMakeVisualStudioGenerators)

    if ($null -ne $activeMajor) {
        $generator = $generators |
            Where-Object { $_.Major -eq $activeMajor } |
            Select-Object -First 1
        if ($generator) {
            if ([string]::IsNullOrWhiteSpace($activeInstance)) {
                $matchingInstallation = $installations |
                    Where-Object { $_.Major -eq $activeMajor } |
                    Select-Object -First 1
                if ($matchingInstallation) {
                    $activeInstance = [string]$matchingInstallation.Path
                }
            }
            return [pscustomobject]@{
                Generator = $generator.Name
                MultiConfig = $true
                VisualStudioGenerator = $true
                Instance = Normalize-VisualStudioInstancePath ([string]$activeInstance)
                CompilerPath = ''
                MakeProgram = ''
                ResourceCompilerPath = ''
                Source = "active Visual Studio $($env:VisualStudioVersion) developer shell"
            }
        }
    }

    foreach ($installation in $installations) {
        $generator = $generators |
            Where-Object { $_.Major -eq $installation.Major } |
            Select-Object -First 1
        if ($generator) {
            return [pscustomobject]@{
                Generator = $generator.Name
                MultiConfig = $true
                VisualStudioGenerator = $true
                Instance = Normalize-VisualStudioInstancePath ([string]$installation.Path)
                CompilerPath = ''
                MakeProgram = ''
                ResourceCompilerPath = ''
                Source = "Visual Studio $($installation.Version) at $($installation.Path)"
            }
        }
    }

    $detectedMajors = @()
    if ($null -ne $activeMajor) {
        $detectedMajors += $activeMajor
    }
    $detectedMajors += @($installations | ForEach-Object { $_.Major })
    $detectedMajors = @($detectedMajors | Sort-Object -Unique -Descending)
    $cmakeGenerators = @($generators | ForEach-Object { $_.Name })

    if ($detectedMajors.Count -gt 0) {
        $detectedText = $detectedMajors -join ', '
        $generatorText = if ($cmakeGenerators.Count -gt 0) {
            $cmakeGenerators -join ', '
        } else {
            '<none>'
        }
        throw "Detected supported-age Visual Studio C++ toolchain major(s) $detectedText, but no usable developer-shell backend or matching Visual Studio generator is available. Update CMake, or open a Visual Studio Developer PowerShell. CMake reports: $generatorText"
    }

    throw "No supported Visual Studio C++ toolchain was detected. Install Visual Studio $minimumVisualStudioMajor.x or newer with the Desktop development with C++ workload."
}

function Test-MingwAvailable {
    return [bool](
        (Get-Command 'g++.exe' -ErrorAction SilentlyContinue) -or
        (Get-Command 'g++' -ErrorAction SilentlyContinue))
}

function Resolve-SquareStarToolchain([string]$RequestedToolchain) {
    switch ($RequestedToolchain) {
        'MinGW' {
            if (-not (Test-MingwAvailable)) {
                throw 'MinGW was requested, but g++ was not detected in PATH. Add your MinGW-w64 bin folder to PATH and reopen the terminal.'
            }
            return 'MinGW'
        }
        'MSVC' {
            if ($null -ne (Get-ActiveVisualStudioDeveloperShellMajor)) {
                return 'MSVC'
            }
            if (@(Get-VisualStudioCppInstallations).Count -gt 0) {
                return 'MSVC'
            }
            throw "MSVC was requested, but no supported Visual Studio C++ toolchain was detected. Install Visual Studio $minimumVisualStudioMajor.x or newer with Desktop development with C++, or use -Toolchain MinGW."
        }
        default {
            # Auto keeps the cheapest/highest-signal checks first: an active
            # developer shell, then MinGW on PATH, then installed Visual Studio.
            if ($null -ne (Get-ActiveVisualStudioDeveloperShellMajor)) {
                return 'MSVC'
            }
            if (Test-MingwAvailable) {
                return 'MinGW'
            }
            if (@(Get-VisualStudioCppInstallations).Count -gt 0) {
                return 'MSVC'
            }
            throw "No supported Windows C++ toolchain was detected. Add 64-bit MinGW-w64 g++ to PATH, or install Visual Studio $minimumVisualStudioMajor.x or newer C++ Build Tools."
        }
    }
}

$requestedToolchain = $Toolchain
$Toolchain = Resolve-SquareStarToolchain $requestedToolchain
if ($requestedToolchain -eq 'Auto') {
    Write-Host "Auto-detected SquareStar toolchain: $Toolchain"
}

function Publish-SingleFilePortableExecutable([string]$PortableRoot, [string]$NameSuffix = '') {
    if (-not (Test-Path -LiteralPath $PortableRoot -PathType Container)) {
        throw "The portable package folder was not found: $PortableRoot"
    }
    $distRoot = Join-Path $projectRoot 'dist'
    New-Item -ItemType Directory -Force -Path $distRoot | Out-Null
    $stagedFiles = @(Get-ChildItem -LiteralPath $PortableRoot -File -Recurse)
    if ($stagedFiles.Count -ne 1 -or $stagedFiles[0].Name -ne 'SquareStar.exe') {
        $found = if ($stagedFiles.Count -eq 0) {
            '<none>'
        } else {
            ($stagedFiles.FullName -join ', ')
        }
        throw "Portable-EXE packaging requires exactly SquareStar.exe; found: $found"
    }
    $executable = Join-Path $distRoot (
        "SquareStar-$projectVersion$NameSuffix-portable-windows-x64.exe")
    Copy-Item -LiteralPath $stagedFiles[0].FullName -Destination $executable -Force
    $executableHash = (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant()
    $checksumPath = "$executable.sha256"
    $checksumLine = "$executableHash  $([IO.Path]::GetFileName($executable))`n"
    [IO.File]::WriteAllText(
        $checksumPath,
        $checksumLine,
        [Text.UTF8Encoding]::new($false))
    Write-Host "Portable executable complete: $executable"
    Write-Host "SHA-256 checksum: $checksumPath"
    return $executable
}

function New-PortablePackageStage {
    $distRoot = [IO.Path]::GetFullPath((Join-Path $projectRoot 'dist'))
    $stageRoot = [IO.Path]::GetFullPath((Join-Path $distRoot 'portable-stage'))
    $requiredPrefix = $distRoot.TrimEnd([IO.Path]::DirectorySeparatorChar) +
        [IO.Path]::DirectorySeparatorChar
    if (-not $stageRoot.StartsWith($requiredPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to reset an unsafe portable stage path: $stageRoot"
    }
    if (Test-Path -LiteralPath $stageRoot) {
        Remove-Item -LiteralPath $stageRoot -Recurse -Force
    }
    New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
    return $stageRoot
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

$mingwOnlyOptions = @(
    @('Small', 'BalancedSize', 'StripDebug', 'StripBinary') |
        Where-Object { $PSBoundParameters.ContainsKey($_) }
)
if ($Toolchain -eq 'MSVC' -and $mingwOnlyOptions.Count -gt 0) {
    throw "The following options require -Toolchain MinGW: $($mingwOnlyOptions -join ', ')"
}
$metadataModeCount = @(
    @($BalancedSize, $StripDebug, $StripBinary) | Where-Object { [bool]$_ }
)
if ($metadataModeCount.Count -gt 1) {
    throw '-BalancedSize, -StripDebug, and -StripBinary are mutually exclusive.'
}
if ($Toolchain -eq 'MSVC' -and $CompilerCache -eq 'On') {
    throw '-CompilerCache On is supported by the MinGW path. Use Auto or Off for MSVC builds.'
}
if ($Test -and $Toolchain -eq 'MSVC') {
    Require-Command 'ctest' 'Install CMake 3.24 or newer and reopen the terminal.'
}

$packageStage = if ($Package) { New-PortablePackageStage } else { $null }

if ($Toolchain -eq 'MinGW') {
    $mingwBuild = Join-Path $PSScriptRoot 'build-mingw.ps1'
    $mingwParameters = @{
        Jobs = $Jobs
        Strict = $strictWarnings
        Optimization = $Optimization
        CompilerCache = $CompilerCache
        BuildTests = [bool]$Test
    }

    foreach ($name in @('Clean', 'Reconfigure', 'FastBuild', 'Small', 'Lto', 'BalancedSize', 'StripDebug', 'StripBinary')) {
        if ($PSBoundParameters.ContainsKey($name)) {
            $mingwParameters[$name] = [bool]$PSBoundParameters[$name]
        }
    }
    if ($Package) {
        $mingwParameters['Install'] = $true
        $mingwParameters['InstallDirectory'] = $packageStage
    } elseif ($Run) {
        $mingwParameters['Run'] = $true
    }

    & $mingwBuild @mingwParameters
    if ($LASTEXITCODE -ne 0) {
        throw "The MinGW build failed with exit code $LASTEXITCODE."
    }
    if ($Package) {
        $executable = Publish-SingleFilePortableExecutable $packageStage
    }
    if ($Package -and $Run) {
        Push-Location $packageStage
        try {
            & $executable
        }
        finally {
            Pop-Location
        }
    }
    return
}

Require-Command 'cmake' 'Install CMake 3.24 or newer and reopen the terminal.'
$msvcGeneratorInfo = Resolve-MsvcCMakeGenerator
$msvcGenerator = [string]$msvcGeneratorInfo.Generator
$msvcMultiConfig = [bool]$msvcGeneratorInfo.MultiConfig
$msvcVisualStudioGenerator = [bool]$msvcGeneratorInfo.VisualStudioGenerator
$msvcGeneratorInstance = Normalize-VisualStudioInstancePath ([string]$msvcGeneratorInfo.Instance)
$msvcCompilerPath = [string]$msvcGeneratorInfo.CompilerPath
$msvcMakeProgram = [string]$msvcGeneratorInfo.MakeProgram
$msvcResourceCompilerPath = [string]$msvcGeneratorInfo.ResourceCompilerPath
Write-Host ("Selected MSVC generator: {0} ({1})." -f $msvcGenerator, $msvcGeneratorInfo.Source)
if (-not [string]::IsNullOrWhiteSpace($msvcGeneratorInstance)) {
    Write-Host ("Pinned Visual Studio instance: {0}" -f $msvcGeneratorInstance)
}
if (-not [string]::IsNullOrWhiteSpace($msvcCompilerPath)) {
    Write-Host ("Pinned MSVC compiler: {0}" -f $msvcCompilerPath)
}
if (-not [string]::IsNullOrWhiteSpace($msvcMakeProgram)) {
    Write-Host ("Pinned MSVC build tool: {0}" -f $msvcMakeProgram)
}
if (-not [string]::IsNullOrWhiteSpace($msvcResourceCompilerPath)) {
    Write-Host ("Pinned Windows SDK resource compiler: {0}" -f $msvcResourceCompilerPath)
}

$buildFlavor = if ($Package) {
    'release'
} elseif ($Test) {
    'test'
} elseif ($FastBuild) {
    'dev'
} else {
    'release'
}
$buildRoot = Join-Path $projectRoot ("build\{0}-msvc" -f $buildFlavor)
$cacheFile = Join-Path $buildRoot 'CMakeCache.txt'
$configureStateFile = Join-Path $buildRoot 'squarestar-msvc-config-state.txt'
function Get-MsvcConfigureState() {
    # Scripted builds suppress generator-owned regeneration, so fingerprint
    # every first-party CMake input that can change the generated build graph.
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
    $inputs += "StrictWarnings|$strictWarnings"
    $inputs += "Optimization|$Optimization"
    $inputs += "Lto|$([bool]$Lto)"
    $inputs += "BuildTests|$([bool]$Test)"
    $inputs += "DependencyCache|$dependencyCacheRoot"
    $inputs += "Generator|$msvcGenerator"
    $inputs += "GeneratorInstance|$msvcGeneratorInstance"
    $inputs += "CompilerPath|$msvcCompilerPath"
    $inputs += "MakeProgram|$msvcMakeProgram"
    $inputs += "ResourceCompilerPath|$msvcResourceCompilerPath"
    $inputs += "MultiConfig|$msvcMultiConfig"
    return [string]::Join("`n", $inputs)
}
$configureState = Get-MsvcConfigureState
$cachedConfigureState = if (Test-Path -LiteralPath $configureStateFile -PathType Leaf) {
    [IO.File]::ReadAllText($configureStateFile)
}
else {
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
$cachedGeneratorInstance = ''
if (Test-Path -LiteralPath $cacheFile -PathType Leaf) {
    $instanceLine = Get-Content -LiteralPath $cacheFile |
        Where-Object { $_ -match '^CMAKE_GENERATOR_INSTANCE:[^=]*=' } |
        Select-Object -First 1
    if ($instanceLine) {
        $cachedGeneratorInstance = ($instanceLine -replace '^CMAKE_GENERATOR_INSTANCE:[^=]*=', '')
    }
}
$generatorChanged = -not [string]::IsNullOrWhiteSpace($cachedGenerator) -and
                    -not $cachedGenerator.Equals($msvcGenerator, [StringComparison]::Ordinal)
$generatorInstanceChanged = -not $cachedGeneratorInstance.Equals(
    $msvcGeneratorInstance, [StringComparison]::OrdinalIgnoreCase)
$needsConfigure = $Clean -or $Reconfigure -or
                  -not (Test-Path -LiteralPath $cacheFile -PathType Leaf) -or
                  -not $cachedConfigureState.Equals($configureState, [StringComparison]::Ordinal)

Push-Location $projectRoot
try {
    if ($needsConfigure) {
        $configureArguments = @(
            '-S', $projectRoot,
            '-B', $buildRoot,
            '-G', $msvcGenerator
        )
        if ($msvcVisualStudioGenerator) {
            $configureArguments += @('-A', 'x64')
            if (-not [string]::IsNullOrWhiteSpace($msvcGeneratorInstance)) {
                # The instance path is normalized above specifically so this
                # native argument cannot end in '' and swallow later options.
                $configureArguments += "-DCMAKE_GENERATOR_INSTANCE=$msvcGeneratorInstance"
            }
        } else {
            $configureArguments += '-DCMAKE_BUILD_TYPE=Release'
            if (-not [string]::IsNullOrWhiteSpace($msvcCompilerPath)) {
                $configureArguments += "-DCMAKE_C_COMPILER:FILEPATH=$msvcCompilerPath"
                $configureArguments += "-DCMAKE_CXX_COMPILER:FILEPATH=$msvcCompilerPath"
            }
            if (-not [string]::IsNullOrWhiteSpace($msvcMakeProgram)) {
                $configureArguments += "-DCMAKE_MAKE_PROGRAM:FILEPATH=$msvcMakeProgram"
            }
            if (-not [string]::IsNullOrWhiteSpace($msvcResourceCompilerPath)) {
                $configureArguments += "-DCMAKE_RC_COMPILER:FILEPATH=$msvcResourceCompilerPath"
            }
        }
        $strictValue = if ($strictWarnings) { 'ON' } else { 'OFF' }
        $ltoValue = if ($Lto) { 'ON' } else { 'OFF' }
        $testValue = if ($Test) { 'ON' } else { 'OFF' }
        $configureArguments += @(
            "-DSQUARESTAR_BUILD_TESTS=$testValue",
                    "-DSQUARESTAR_DEPENDENCY_CACHE_DIR:PATH=$dependencyCacheRoot",
            "-DSQUARESTAR_STRICT_WARNINGS=$strictValue",
            "-DSQUARESTAR_OPTIMIZATION=$Optimization",
            "-DSQUARESTAR_ENABLE_LTO=$ltoValue"
        )
        if ($Clean -or $Reconfigure -or $generatorChanged -or
            $generatorInstanceChanged -or -not (Test-Path -LiteralPath $cacheFile -PathType Leaf)) {
            $configureArguments += '--fresh'
        }

        Write-Host "Configuring SquareStar MSVC $buildFlavor tree with $msvcGenerator..."
        $configureTimer = [Diagnostics.Stopwatch]::StartNew()
        & cmake @configureArguments
        $configureTimer.Stop()
        if ($LASTEXITCODE -ne 0) {
            throw "CMake configuration failed with exit code $LASTEXITCODE using $msvcGenerator. SquareStar now initializes the x64 Visual Studio compiler environment and pins cl.exe directly when a command-line backend is available. Check the CMake error above; if it mentions rc.exe, kernel32.lib, ucrt, or Windows SDK files, repair/install the Windows SDK component in the selected Visual Studio C++ workload."
        }
        Write-Host ("Configure time: {0:N1}s" -f $configureTimer.Elapsed.TotalSeconds)
        [IO.File]::WriteAllText($configureStateFile, $configureState)
    }
    else {
        Write-Host "Reusing the existing MSVC $buildFlavor CMake configuration."
    }

    Write-Host "Building SquareStar with $Jobs parallel job(s)..."
    $buildArguments = @('--build', $buildRoot)
    if ($msvcMultiConfig) {
        $buildArguments += @('--config', 'Release')
    }
    $buildArguments += @('--parallel', $Jobs)
    if ($FastBuild -and -not $Test) {
        $buildArguments += @('--target', 'SquareStar')
    }
    if ($Clean) {
        $buildArguments += '--clean-first'
    }
    $compileTimer = [Diagnostics.Stopwatch]::StartNew()
    & cmake @buildArguments
    $compileTimer.Stop()
    if ($LASTEXITCODE -ne 0) {
        throw "SquareStar build failed with exit code $LASTEXITCODE."
    }
    Write-Host ("Compile/link time: {0:N1}s" -f $compileTimer.Elapsed.TotalSeconds)

    if ($Test) {
        # A failed release gate must not leave a freshly installed/packaged
        # executable that can be mistaken for a validated artifact.
        $testArguments = @('--test-dir', $buildRoot, '--output-on-failure', '--parallel', $Jobs)
        if ($msvcMultiConfig) {
            $testArguments += @('-C', 'Release')
        }
        & ctest @testArguments
        if ($LASTEXITCODE -ne 0) {
            throw "SquareStar tests failed with exit code $LASTEXITCODE."
        }
    }

    if (-not $FastBuild -or $Package) {
        $installArguments = @('--install', $buildRoot)
        if ($msvcMultiConfig) {
            $installArguments += @('--config', 'Release')
        }
        if ($Package) {
            $installArguments += @('--prefix', $packageStage)
            Write-Host "Creating a fresh portable package stage: $packageStage"
        } else {
            Write-Host 'Creating the portable prebuilt folder...'
        }
        $installTimer = [Diagnostics.Stopwatch]::StartNew()
        & cmake @installArguments
        $installTimer.Stop()
        if ($LASTEXITCODE -ne 0) {
            throw "SquareStar installation failed with exit code $LASTEXITCODE."
        }
        Write-Host ("Install time: {0:N1}s" -f $installTimer.Elapsed.TotalSeconds)
    } else {
        Write-Host 'Fast Dev Build: skipping cmake --install.'
    }
}
finally {
    Pop-Location
}

$executable = if ($FastBuild -and -not $Package) {
    if ($msvcMultiConfig) {
        Join-Path $buildRoot 'Release\SquareStar.exe'
    } else {
        Join-Path $buildRoot 'SquareStar.exe'
    }
} elseif ($Package) {
    Join-Path $packageStage 'SquareStar.exe'
} else {
    Join-Path $projectRoot 'prebuilt\SquareStar.exe'
}
if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "The build completed without producing $executable."
}

if ($Package) {
    $executable = Publish-SingleFilePortableExecutable $packageStage
}


$overallBuildTimer.Stop()
Write-Host ("Total build pipeline: {0:N1}s" -f $overallBuildTimer.Elapsed.TotalSeconds)
Write-Host "Build complete: $executable"
if ($Run) {
    Write-Host 'Starting SquareStar...'
    Push-Location (Split-Path -Parent $executable)
    try {
        & $executable
    }
    finally {
        Pop-Location
    }
}
