# Architecture

SquareStar has two front ends, Full GUI and LiteGUI. They share market data, application state, services, alerts, and persistence.

## Directories

| Directory | Contains |
| --- | --- |
| `domain` | market values and pure rules |
| `application` | state, navigation, and application decisions |
| `services` | HTTP, providers, parsing, persistence, credentials |
| `platform` | Win32, GLFW, filesystem, audio |
| `presentation` | chart/export models, formatting, layout |
| `modules` | UI features and shell code |

Keep provider formats out of UI code and keep local UI changes out of the networking layer.

## Runtime rules

### Quotes

`currentPrice` and `quoteTimestamp` describe one displayed quote. A timestamp-only update cannot make an older price appear newer. Quote merging happens before the GUI sees the data, so Full GUI and LiteGUI get the same result.

Yahoo is preferred for the displayed quote. Finnhub fills gaps when its optional key-backed path is available.

### Feed wording

The UI may show an **As of** time. It does not label a feed `Live`, `Realtime`, or `Delayed` from timestamp age alone.

### UI thread

Provider/network work runs off the UI thread. Completed jobs update application state and request a redraw.

### Rendering

If nothing needs a frame, the GUI sleeps. Code that needs another frame either requests one or supplies the next wake time.

### Persistence

Saved state goes through the config save queue. Sensitive local state, including ticker-bearing ImGui layout state, and the Finnhub key are protected with Windows DPAPI before they are written.

### Workspace switching

Full GUI and LiteGUI switch in-process. The window/view state changes; market data, alerts, services, and saved state remain shared.

## Network boundary

In-process HTTP is limited to the Yahoo/Finnhub API hosts used by SquareStar. Publisher and company pages open in the system browser.

## Third-party code

`third_party/` contains vendored dependencies. SquareStar carries a small cursor-sync change in Dear ImGui's GLFW backend for the event-driven loop; check that patch when updating ImGui.

## Validation

`build.cmd validate` is the full Windows check. `portable-release` covers the portable component suite but does not exercise the Windows shell.
