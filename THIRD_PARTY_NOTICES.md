# Third-Party Notices

SquareStar includes or builds against third-party software and bundles third-party font/audio assets. Each component remains subject to its upstream license.

## Software

| Component | Version / snapshot | Inclusion | License |
| --- | --- | --- | --- |
| Dear ImGui | 1.93.0 WIP snapshot | Vendored source | MIT |
| ImPlot | 1.1 WIP snapshot | Vendored source | MIT |
| yyjson | 0.12.0 | Vendored source | MIT |
| GLFW | 3.5.1 | Pinned CMake dependency | zlib/libpng |
| curl | 8.22.0 | Pinned CMake dependency | curl license |
| Outfit | Bundled font files | Bundled assets | SIL Open Font License 1.1 |

GLFW and curl are fetched from pinned source archives with SHA-256 verification in the CMake build.

SquareStar's vendored Dear ImGui GLFW backend contains a small local cursor-synchronization change used by the event-driven rendering path. It is not a byte-for-byte upstream tree.

## Assets

Audio/font/icon attribution is listed in [`ASSET_CREDITS.md`](ASSET_CREDITS.md). Required license texts are kept in `licenses/` and applicable notices are embedded in the portable executable for the in-app **Credits & Licenses** view.

When redistributing SquareStar, keep the applicable upstream notices and license texts.
