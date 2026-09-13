[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Exe,

    [ValidateRange(5, 10000)]
    [int]$Runs = 100,

    [ValidateRange(0, 1000)]
    [int]$Warmups = 10,

    [ValidateRange(5, 600)]
    [int]$IdleSeconds = 60,

    [ValidateRange(5, 200)]
    [int]$StartupRuns = 30,

    [string]$OutputRoot
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$projectRoot = Split-Path -Parent $PSScriptRoot
$exePath = [IO.Path]::GetFullPath($Exe)
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    throw "SquareStar executable not found: $exePath"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $projectRoot 'benchmarks\results'
}
$outputRootPath = [IO.Path]::GetFullPath($OutputRoot)
$stamp = (Get-Date).ToUniversalTime().ToString('yyyyMMdd-HHmmss')
$runDirectory = Join-Path $outputRootPath $stamp
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

function Invoke-SquareStarBenchmark {
    param([string[]]$Arguments)
    & $exePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "SquareStar benchmark command failed with exit code $LASTEXITCODE: $($Arguments -join ' ')"
    }
}

function Get-Median {
    param([double[]]$Values)
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { return 0.0 }
    $middle = [int][math]::Floor($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) {
        return [double]$sorted[$middle]
    }
    return ([double]$sorted[$middle - 1] + [double]$sorted[$middle]) / 2.0
}

function Get-NearestRankPercentile {
    param(
        [double[]]$Values,
        [double]$Percentile
    )
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { return 0.0 }
    $rank = [int][math]::Ceiling($Percentile * $sorted.Count)
    $index = [math]::Max(0, [math]::Min($sorted.Count - 1, $rank - 1))
    return [double]$sorted[$index]
}

Write-Host ''
Write-Host '=== SquareStar offline/local benchmark ==='
Invoke-SquareStarBenchmark -Arguments @(
    '--benchmark',
    '--benchmark-output', $runDirectory,
    '--benchmark-runs', $Runs,
    '--benchmark-warmups', $Warmups
)

$earlySystemText = Get-Content -LiteralPath (Join-Path $runDirectory 'system.txt') -Raw
if ($earlySystemText -match '(?m)^build_mode:\s*Debug\s*$') {
    throw 'Benchmark report requires a Release build.'
}
$optimizationMatch = [regex]::Match(
    $earlySystemText,
    '(?m)^configured_release_optimization:\s*(\S+)\s*$')
if (-not $optimizationMatch.Success) {
    throw 'Could not verify the configured optimization from system.txt.'
}
if ($optimizationMatch.Groups[1].Value -ne 'O2') {
    $message = ('Benchmark report requires O2; system.txt reports {0}.') -f $optimizationMatch.Groups[1].Value
    throw $message
}

$startupCsv = Join-Path $runDirectory 'startup.csv'
if (Test-Path -LiteralPath $startupCsv) {
    Remove-Item -LiteralPath $startupCsv -Force
}
Write-Host ''
Write-Host "=== Startup benchmark: $StartupRuns independent processes ==="
Write-Host 'The SquareStar window will briefly appear once per run. Do not interact with it.'
for ($i = 1; $i -le $StartupRuns; $i++) {
    Write-Host ("Startup sample {0}/{1}" -f $i, $StartupRuns)
    Invoke-SquareStarBenchmark -Arguments @(
        '--benchmark-startup',
        '--benchmark-output', $runDirectory
    )
}

$idleCsv = Join-Path $runDirectory 'idle.csv'
if (Test-Path -LiteralPath $idleCsv) {
    Remove-Item -LiteralPath $idleCsv -Force
}
Write-Host ''
Write-Host "=== Settled idle benchmark: $IdleSeconds seconds ==="
Write-Host 'Leave the SquareStar window visible and do not move the mouse over it during this sample.'
Invoke-SquareStarBenchmark -Arguments @(
    '--benchmark-idle',
    '--benchmark-seconds', $IdleSeconds,
    '--benchmark-output', $runDirectory
)

$memoryCsv = Join-Path $runDirectory 'memory-layers.csv'
if (Test-Path -LiteralPath $memoryCsv) {
    Remove-Item -LiteralPath $memoryCsv -Force
}
Write-Host ''
Write-Host '=== Staged memory benchmark ==='
Write-Host 'This probe performs no outbound network I/O; it only initializes the network runtime/workers.'
Invoke-SquareStarBenchmark -Arguments @(
    '--benchmark-memory',
    '--benchmark-output', $runDirectory
)

$offline = @(Import-Csv -LiteralPath (Join-Path $runDirectory 'benchmark-results.csv'))
$startup = @(Import-Csv -LiteralPath $startupCsv)
$idle = @(Import-Csv -LiteralPath $idleCsv)
$memory = @(Import-Csv -LiteralPath $memoryCsv)
if ($startup.Count -ne $StartupRuns) {
    throw "Expected $StartupRuns startup rows, found $($startup.Count)."
}
if ($idle.Count -lt 1) {
    throw 'Idle benchmark produced no rows.'
}
if ($memory.Count -ne 8) {
    throw "Expected 8 staged memory rows (A-H), found $($memory.Count)."
}
if (@($offline | Where-Object { $_.verified -ne 'PASS' }).Count -gt 0 -or
    @($startup | Where-Object { $_.verified -ne 'PASS' }).Count -gt 0 -or
    @($idle | Where-Object { $_.verified -ne 'PASS' }).Count -gt 0) {
    throw 'At least one benchmark verification failed.'
}

$startupProcessValues = [double[]]@($startup | ForEach-Object { [double]$_.process_creation_to_first_visible_present_ms })
$startupMainValues = [double[]]@($startup | ForEach-Object { [double]$_.main_entry_to_first_visible_present_ms })
$startupProcessMedian = Get-Median $startupProcessValues
$startupProcessP95 = Get-NearestRankPercentile $startupProcessValues 0.95
$startupProcessP99 = Get-NearestRankPercentile $startupProcessValues 0.99
$startupProcessMin = ($startupProcessValues | Measure-Object -Minimum).Minimum
$startupProcessMax = ($startupProcessValues | Measure-Object -Maximum).Maximum
$startupMainMedian = Get-Median $startupMainValues
$startupMainP95 = Get-NearestRankPercentile $startupMainValues 0.95
$idleRow = $idle[-1]

$systemText = Get-Content -LiteralPath (Join-Path $runDirectory 'system.txt') -Raw
$systemLines = @($systemText -split "`r?`n" | Where-Object { $_ -match ':' })
$systemMap = @{}
foreach ($line in $systemLines) {
    $parts = $line -split ':', 2
    if ($parts.Count -eq 2) {
        $systemMap[$parts[0].Trim()] = $parts[1].Trim()
    }
}
$cpu = if ($systemMap.ContainsKey('cpu')) { $systemMap['cpu'] } else { 'unknown' }
$os = if ($systemMap.ContainsKey('os')) { $systemMap['os'] } else { 'unknown' }
$compiler = if ($systemMap.ContainsKey('compiler')) { $systemMap['compiler'] } else { 'unknown' }
$buildMode = if ($systemMap.ContainsKey('build_mode')) { $systemMap['build_mode'] } else { 'unknown' }
$optimization = if ($systemMap.ContainsKey('configured_release_optimization')) { $systemMap['configured_release_optimization'] } else { 'unknown' }
$gpu = if ($systemMap.ContainsKey('gpu_d3d11_adapter')) { $systemMap['gpu_d3d11_adapter'] } else { 'unknown' }
$d3dFeature = if ($systemMap.ContainsKey('d3d_feature_level')) { $systemMap['d3d_feature_level'] } else { 'unknown' }
$framebuffer = if ($systemMap.ContainsKey('gui_framebuffer_size')) { $systemMap['gui_framebuffer_size'] } else { 'unknown' }

$markdown = New-Object System.Collections.Generic.List[string]
$markdown.Add('# SquareStar benchmark results')
$markdown.Add('')
$markdown.Add("Generated UTC: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-dd HH:mm:ss'))")
$markdown.Add('')
$markdown.Add('## Environment')
$markdown.Add('')
$markdown.Add("- CPU: $cpu")
$markdown.Add("- OS: $os")
$markdown.Add("- Compiler: $compiler")
$markdown.Add("- Build: $buildMode, configured optimization $optimization")
$markdown.Add("- GPU/D3D11 adapter: $gpu")
$markdown.Add("- D3D feature level: $d3dFeature")
$markdown.Add("- GUI framebuffer: $framebuffer")
$markdown.Add('')
$markdown.Add('## Method')
$markdown.Add('')
$markdown.Add("- Offline chart tests: deterministic synthetic input generated outside the timed region; $Warmups warmups + $Runs timed runs per case.")
$markdown.Add('- Offline timing reports the first call separately, then median/p95/p99 for steady-state calls. Provider/network I/O is excluded.')
$markdown.Add("- Startup: $StartupRuns independent processes; the primary timer starts at Windows process creation and stops after the first visible host-window Direct3D Present completes. A secondary main()-entry timer is also recorded.")
$markdown.Add('- Startup uses in-memory defaults and skips provider/network, audio, config load/save, and layout persistence.')
$markdown.Add("- Idle: starts after the first visible frame and samples the settled Home surface for $IdleSeconds seconds. Provider/network, audio, animations, config I/O, and layout persistence are suppressed.")
$markdown.Add('- Memory layers: one deterministic startup process records A-H Private Bytes/working set boundaries. Stage G initializes curl + worker executors without DNS, sockets, TLS handshakes, or HTTP requests.')
$markdown.Add('- CPU is process CPU time divided by wall time and logical processor count.')
$markdown.Add('')
$markdown.Add('## Local chart / LOD')
$markdown.Add('')
$markdown.Add('| Case | Input | Output | First call ms | Median ms | p95 ms | p99 ms |')
$markdown.Add('|---|---:|---:|---:|---:|---:|---:|')
foreach ($row in $offline) {
    $name = "$($row.suite)/$($row.case)"
    $markdown.Add(("| {0} | {1:N0} | {2:N0} | {3:N3} | {4:N3} | {5:N3} | {6:N3} |" -f
        $name,
        [double]$row.input_points,
        [double]$row.output_points,
        [double]$row.cold_ms,
        [double]$row.median_ms,
        [double]$row.p95_ms,
        [double]$row.p99_ms))
}
$markdown.Add('')
$markdown.Add('## Startup')
$markdown.Add('')
$markdown.Add(("- process creation -> first visible present: **{0:N2} ms median**, {1:N2} ms p95, {2:N2} ms p99 (min {3:N2}, max {4:N2}, n={5})" -f
    $startupProcessMedian, $startupProcessP95, $startupProcessP99, $startupProcessMin, $startupProcessMax, $StartupRuns))
$markdown.Add(("- main() entry -> first visible present: **{0:N2} ms median**, {1:N2} ms p95" -f
    $startupMainMedian, $startupMainP95))
$markdown.Add('')
$markdown.Add('## Settled idle')
$markdown.Add('')
$markdown.Add(("- CPU: **{0:N3}%** of total machine capacity" -f [double]$idleRow.cpu_percent_total_machine))
$markdown.Add(("- Working set: **{0:N1} MB**" -f [double]$idleRow.working_set_mb))
$markdown.Add(("- Private bytes: **{0:N1} MB**" -f [double]$idleRow.private_bytes_mb))
$markdown.Add(("- Frames actually presented during the {0:N1}s idle window: **{1}** ({2:N3}/s)" -f
    [double]$idleRow.duration_seconds,
    $idleRow.presented_frames_during_window,
    [double]$idleRow.presented_frames_per_second))
$markdown.Add('')
$markdown.Add('## Staged memory layers')
$markdown.Add('')
$markdown.Add('| Stage | Boundary | Private MB | Delta MB | From A MB | Working set MB |')
$markdown.Add('|---|---|---:|---:|---:|---:|')
foreach ($row in $memory) {
    $markdown.Add(("| {0} | {1} | {2:N2} | {3:N2} | {4:N2} | {5:N2} |" -f
        $row.stage,
        $row.label,
        [double]$row.private_bytes_mb,
        [double]$row.delta_private_mb,
        [double]$row.delta_from_a_mb,
        [double]$row.working_set_mb))
}
$markdown.Add('')
$markdown.Add('`loaded-modules.txt` is captured at stage H and explicitly reports whether `opengl32.dll` or a common vendor OpenGL ICD was loaded.')
$markdown.Add('')
$markdown.Add('## Scope')
$markdown.Add('')
$markdown.Add('These are local measurements. Provider/network timing is excluded. Raw CSV files and environment details are kept with the report.')
$markdown.Add('')
$markdown.Add('Compare runs only with the machine, build, and measurement scope in mind.')

$reportPath = Join-Path $runDirectory 'benchmark-report.md'
[IO.File]::WriteAllLines($reportPath, $markdown, [Text.UTF8Encoding]::new($false))

Write-Host ''
Write-Host '=== Done ==='
Write-Host "Raw + summary results: $runDirectory"
Write-Host "Benchmark report: $reportPath"
Write-Host ''
Get-Content -LiteralPath $reportPath
