# Source tree

| Directory | Main contents |
| --- | --- |
| `domain/` | market values and pure rules |
| `application/` | app state, navigation, request/refresh rules |
| `services/` | providers, HTTP, parsing, persistence |
| `platform/` | Win32, GLFW, filesystem, audio |
| `presentation/` | charts, export, formatting, layout |
| `modules/` | Full GUI/LiteGUI and shell code |

Files directly under `src/` are entry points, resources, the benchmark runner, or the PCH.

Useful starting points:

- quote merging: `application/stock_data_merge.*`, `services/stock_provider_merge.*`
- provider parsing: `services/`
- refresh/session rules: `application/`
- notification center: `modules/notification_center.*`
- chart rendering/LOD: `presentation/` and `modules/charts.*`
- Windows-specific code: `platform/`

## CMake source discovery

Production `.cpp`/`.hpp` files are discovered by directory. Delete obsolete translation units instead of leaving them under a discovered source directory.

`cmake/SquareStarSourceGraph.cmake` contains the portable-test source exclusions used by CI.

See [`../docs/architecture/ARCHITECTURE.md`](../docs/architecture/ARCHITECTURE.md).
