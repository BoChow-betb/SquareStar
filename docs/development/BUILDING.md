# Building SquareStar

`build.cmd` is the Windows build entry point. Run it without arguments for the build menu.

## Requirements

- 64-bit Windows
- CMake 3.24+
- Visual Studio 17.x or newer / Build Tools with **Desktop development with C++**, or 64-bit MinGW-w64

The build script accepts Visual Studio 17.x and newer supported releases, including Visual Studio 18.x / 2026. Visual Studio versions older than 17.x are intentionally not supported. MinGW `g++` on `PATH` is also detected.

For MSVC builds, `build.cmd` normalizes the selected Visual Studio installation into an x64 compiler environment by importing `VC\Auxiliary\Build\vcvars64.bat`. This fixes Developer PowerShell sessions that were opened for x86 and avoids relying on Visual Studio generator auto-discovery. The build then uses the exact `cl.exe` through Ninja when available, with NMake as the fallback. `cl.exe`, the build tool, and the Windows SDK resource compiler are pinned explicitly for CMake.

From a normal terminal, the script uses `vswhere` to locate Visual Studio 17.x or newer and bootstraps the same x64 command-line environment. A matching Visual Studio generator is retained only as a fallback if a command-line backend cannot be established. Instance paths are still normalized without a trailing slash. Visual Studio 18 / 2026 generator support begins with CMake 4.2, but the preferred direct `cl.exe` path does not depend on the Visual Studio generator.

The `windows-msvc-release` CMake preset is primarily the CI/direct-CMake path and leaves Visual Studio generator selection to CMake. `build.cmd` is the recommended contributor entry point because it performs the additional shell/instance selection described above.

### PowerShell execution policy

`build.cmd` starts the repository's checked-in PowerShell build scripts with a process-scoped
`-ExecutionPolicy Bypass`. This is only for the child `powershell.exe` process created by
`build.cmd`; it does not change the user or machine execution policy and does not write an
execution-policy setting to the registry. Organization-enforced Group Policy can still take
precedence.

## Build menu

```cmd
build.cmd
```

```text
1 = Quick dev build
2 = Release build
```

## Developer build

```cmd
build.cmd fast
```

This uses:

- CMake `Release` / `NDEBUG`
- `O2` by default
- the `build\dev-*` tree
- incremental builds
- no install/package step

Typical executable locations:

```text
MinGW:                         build\dev-mingw\SquareStar.exe
MSVC (Developer PowerShell):      build\dev-msvc\SquareStar.exe
MSVC (Visual Studio generator):   build\dev-msvc\Release\SquareStar.exe
```

## Commands

```cmd
build.cmd
build.cmd fast
build.cmd validate
build.cmd release
build.cmd clean
build.cmd help
```

- `build.cmd` — build menu
- `build.cmd fast` — incremental developer build
- `build.cmd validate` — strict build and tests
- `build.cmd release` — build, test, and create the portable package
- `build.cmd clean` — remove generated build/package/cache data
- `build.cmd help` — list build options

## Build options

`fast`, `release`, and direct builds accept these options:

| Parameter | Values | Default |
|---|---|---|
| `-Toolchain` | `Auto`, `MSVC`, `MinGW` | `Auto` |
| `-Jobs` | integer `1..256` | logical processor count |
| `-Optimization` | `O1`, `O2`, `O3`, `Os` | `O2` |
| `-CompilerCache` | `Auto`, `On`, `Off` | `Auto` |

Switches:

```text
-Clean
-Reconfigure
-FastBuild
-Lto
-Strict
-Test
-Package
-Run
```

MinGW size/metadata switches:

```text
-Small
-BalancedSize
-StripDebug
-StripBinary
```

`-Small` is an alias for `-Optimization Os`. `-BalancedSize` also requires `Os`.
MinGW Release builds are fully stripped at link time with `-s` by default; `-StripBinary`
remains as an explicit selector for that same mode. `-BalancedSize` and `-StripDebug`
replace the default full-strip mode when selected. `-BalancedSize`, `-StripDebug`, and
`-StripBinary` cannot be combined.

Examples:

```cmd
build.cmd fast -Toolchain MinGW -Jobs 8
build.cmd fast -Optimization O3
build.cmd -Toolchain MSVC -Test -Strict -Run
build.cmd release
```

Options can also be passed directly:

```cmd
build.cmd -Toolchain MinGW -Optimization O2 -Jobs 8
```

Use `build.cmd fast ...` when you want the developer build tree.

## Validation

`build.cmd validate` accepts:

| Parameter | Values | Default |
|---|---|---|
| `-Toolchain` | `Auto`, `MSVC`, `MinGW`, `Both` | `Auto` |
| `-Jobs` | integer `1..256` | logical processor count |
| `-Clean` | switch | off |

Example:

```cmd
build.cmd validate -Toolchain Both -Clean
```

Unsupported parameters return an error.

## Outputs

Development executables:

- MSVC, Developer PowerShell backend: `build\dev-msvc\SquareStar.exe`
- MSVC, Visual Studio generator backend: `build\dev-msvc\Release\SquareStar.exe`
- MinGW: `build\dev-mingw\SquareStar.exe`

Release packages are written under `dist\`.

## Portable tests

The non-Windows components have one shared preset:

```sh
cmake --preset portable-release
cmake --build --preset portable-release
ctest --preset portable-release
```

This does not build the Win32/GLFW application shell.

## Warnings and sanitizers

Useful CMake options:

```text
-DSQUARESTAR_STRICT_WARNINGS=ON
-DSQUARESTAR_ENABLE_SANITIZERS=ON
```

Sanitizers apply to the portable path where supported.

## Finnhub

A Finnhub key is not required to build or run tests. Add one in the app only for Finnhub-backed runtime features.

## Optimization

O2 is the default for developer and release builds. O1, O3, Os, and LTO are opt-in.

Profile-guided optimization (PGO) is **not** part of the current build pipeline. There is no `build.cmd` PGO mode and no supported PGO training/optimize workflow in the checked-in CMake presets.

See [`PERFORMANCE.md`](PERFORMANCE.md) for benchmark details.
