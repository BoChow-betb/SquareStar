# Contributing

Small fixes are welcome. You do not need to learn the whole codebase before changing one part of it.

## Build and test

On Windows:

```cmd
build.cmd fast
```

Before opening a pull request:

```cmd
build.cmd validate
```

Portable components can also be built with CMake + Ninja:

```sh
cmake --preset portable-release
cmake --build --preset portable-release
ctest --preset portable-release
```

Build options are documented in [`docs/development/BUILDING.md`](docs/development/BUILDING.md).

## Where changes go

- `domain/` — market rules and values
- `application/` — state, navigation, refresh/request rules
- `services/` — providers, networking, parsing, persistence
- `platform/` — Windows, GLFW, filesystem, audio
- `presentation/` — charts, export, formatting, layout
- `modules/` — visible UI and shell code

Prefer a direct function over a one-use abstraction, early returns over deep nesting, and avoid unrelated formatting changes in functional patches.

## Source discovery

CMake discovers production sources by directory. Removing a feature means removing its dead `.cpp` files too; otherwise an old translation unit can be picked up again by the glob.

If a new component depends on the Windows/GLFW/curl host, add it to the portable exclusions in `cmake/SquareStarSourceGraph.cmake`.

## Pull requests

Add a regression test when it is practical. In the PR, say what changed and how you tested it.

Do not commit API keys, credentials, runtime data, build/benchmark output, or provider-data dumps. Keep third-party licenses and attribution with imported code/assets.
