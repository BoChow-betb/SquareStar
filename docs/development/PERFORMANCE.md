# Performance and benchmarks

SquareStar uses O2 for normal developer and release builds. LTO and size-oriented MinGW modes are optional.

## Offline chart/LOD benchmark

Build the MinGW developer executable:

```cmd
build.cmd fast
```

Then run:

```powershell
cd build\dev-mingw
.\SquareStar.exe --benchmark
```

The offline benchmark does not initialize the GUI or contact Yahoo/Finnhub. It measures the production chart LOD code with deterministic synthetic input generated before timing:

- LTTB downsampling to 2,048 points
- line-chart LOD at 1,600 logical pixels / 1x scale
- candlestick bucket LOD at the same width
- 10k, 100k, and 1M input points

The first call is reported separately because it includes output allocation. The default steady-state run uses 5 warmups and 50 timed samples and reports median, p95, and p99.

Each case has a correctness check. A failed check returns a non-zero exit code.

Without `--benchmark-output`, files are written to:

```text
benchmark-results\<UTC timestamp>\
```

The folder contains:

```text
benchmark-results.csv
benchmark-samples.csv
benchmark-summary.txt
system.txt
```

### Options

```powershell
.\SquareStar.exe --benchmark-help
```

| Option | Values | Default | Mode |
|---|---|---:|---|
| `--benchmark-runs N` | `5..10000` | `50` | offline |
| `--benchmark-warmups N` | `0..1000` | `5` | offline |
| `--benchmark-seconds N` | `5..600` | `30` | idle |
| `--benchmark-output DIR` | directory path | timestamped folder | all |

Example:

```powershell
.\SquareStar.exe --benchmark --benchmark-runs 100 --benchmark-warmups 10
```

### Statistics and scope

Median uses the ordinary midpoint rule for an even number of samples. p95/p99 use nearest-rank.

With 50 timed samples, nearest-rank p99 is the slowest sample. Use more samples if tail latency matters.

The offline benchmark measures CPU-side chart/LOD work only. It does not measure provider/network latency, application startup, or full GUI/GPU frame time.

## Startup probe

```powershell
.\SquareStar.exe --benchmark-startup
```

The normal Direct3D 11/GLFW/ImGui startup path runs until the first visible host-window Direct3D `Present()` completes, then exits. Results are appended to `startup.csv`.

Two timers are recorded:

```text
Windows process creation -> first visible host-window present
main() entry             -> first visible host-window present
```

The probe uses in-memory defaults and skips provider warmup, audio, config load/save, so repeated runs are comparable.

For a useful startup distribution, run separate processes rather than repeating the timer inside one process.

## Controlled idle probe

```powershell
.\SquareStar.exe --benchmark-idle --benchmark-seconds 30
```

The controlled idle probe starts after the first visible frame and records:

- process CPU percentage across total machine capacity
- working-set memory
- private bytes
- presented frame count

Provider work, audio, animations, config I/O, and normal network warmup are disabled during the probe. Results are appended to `idle.csv`.

Do not interact with the window during the sample.

This number is intentionally **not** "home page idle" and should not be presented as normal UI memory usage. The benchmark removes work and state that a normal interactive process keeps so that repeated runs are easier to compare.

## Fresh-launch idle

Use this measurement when describing the RAM footprint a user is likely to see immediately after opening SquareStar:

1. Start a fresh normal `SquareStar.exe` process with no benchmark flags.
2. Leave the initial view untouched; do not switch tabs, search, or open a symbol.
3. Wait for transient startup allocations and idle cleanup to settle.
4. Record **Private Bytes / Private Usage** in the same process monitor.

On the current reference build, the fresh-launch idle spot-check is **11.44 MB Private Bytes**. Normal stock-view use is roughly **13–15 MB** in the accompanying spot-checks, reaching **14.77 MB** with four stock tabs open during active use. These are observations, not hard targets: GPU/driver DLLs, injected utilities, Windows builds, allocator state, and network/provider state can change the values.

On the reference Intel Core i5-8265U, ordinary interactive spot-checks typically stay below about **2% CPU**; the four-stock-tab active screenshot shows **1.14% CPU**. Treat that as a machine-specific observation rather than a universal ceiling or a stress-test result.

When reporting results, call the initial state **fresh-launch idle** (or "fresh launch, left idle"), not "home page idle". That wording describes how the measurement was produced rather than implying that a particular page itself has a fixed memory cost.

## Full benchmark run

The helper script runs the offline benchmark, multiple startup processes, and one idle sample:

```powershell
.\scripts\run-benchmarks.ps1 `
    -Exe .\build\dev-mingw\SquareStar.exe `
    -Runs 100 `
    -Warmups 10 `
    -StartupRuns 30 `
    -IdleSeconds 60
```

For MSVC:

```powershell
.\scripts\run-benchmarks.ps1 `
    -Exe .\build\dev-msvc\Release\SquareStar.exe
```

Parameters:

| Parameter | Values | Default |
|---|---|---:|
| `-Runs` | `5..10000` | `100` |
| `-Warmups` | `0..1000` | `10` |
| `-StartupRuns` | `5..200` | `30` |
| `-IdleSeconds` | `5..600` | `60` |
| `-OutputRoot` | directory path | `benchmarks\results` |

The script requires a Release/NDEBUG O2 build and stops if a benchmark verification fails.

Each run gets a timestamped directory containing the raw CSV files, environment details, and `benchmark-report.md`.

## Reporting notes

When sharing results, include the machine/build details and raw samples. Keep provider/network timing separate from the local chart, startup, and idle measurements.

Use the same executable and power mode across runs. If repeated runs differ substantially, check background load or thermal throttling before drawing conclusions.
