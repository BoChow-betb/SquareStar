# Privacy

SquareStar is a local Windows desktop application. It does not include advertising, behavioral analytics, crash-report upload, or a telemetry SDK.

## Local data

SquareStar uses lazy portable storage. It does not create a `data` folder merely because a clean first-run copy was opened and closed. When a feature actually needs persistent storage, app-owned runtime files are kept under `data` beside `SquareStar.exe` rather than in `%LOCALAPPDATA%` or a SquareStar-specific system temp directory. Settings are stored in `data\config.json`, public caches under `data\cache`, diagnostics under `data\logs`, and the default export location is `data\exports`.

The Finnhub API key is protected at rest with Windows DPAPI. Privacy-sensitive state such as watchlists, saved searches, alerts, saved tabs, the configured export directory, and the ImGui window/layout state is also protected before it is written to `config.json`. The layout is protected because window identifiers can contain ticker symbols. Because DPAPI is tied to the Windows user/machine context, copying an existing protected config to another Windows account or machine may require re-entering the API key and rebuilding private state.

Search history is optional and can be disabled or cleared in **Settings -> Privacy**.

SquareStar may also keep a bounded public screener/trending cache and small rotating diagnostic logs. Diagnostic entries are local, sanitized, and do not have an upload path in the current source.

**Clear app data** removes SquareStar's saved state, cache, diagnostic logs, temporary files, and recovery copies while keeping user-created exports. Deleting the entire folder containing `SquareStar.exe` and `data` removes all SquareStar-owned portable files in that folder. Normal file deletion is not forensic secure erasure.

## Network requests

SquareStar currently makes HTTPS requests to:

- `query1.finance.yahoo.com` and `query2.finance.yahoo.com` for market data;
- `fc.yahoo.com` for Yahoo cookie/crumb bootstrap; and
- `finnhub.io` for features that use a user-supplied Finnhub key.

Depending on the feature, providers may receive ticker symbols, chart/screener parameters, search text, a Finnhub API key, and ordinary HTTPS connection metadata such as the source IP address. SquareStar does not send the Finnhub key to Yahoo Finance.

Yahoo cookie/crumb state is kept in memory rather than a persistent cookie file.

## External links

News/company pages are opened in the user's system browser after passing SquareStar's external HTTPS URL policy. Those sites then operate under their own privacy and cookie rules.

## Scope

Yahoo Finance, Finnhub, and browser destinations are independent third parties. Their behavior and terms can change independently of this project.
