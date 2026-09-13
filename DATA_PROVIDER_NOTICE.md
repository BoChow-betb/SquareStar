# Market-data provider notice

SquareStar is an independent open-source desktop client. It requests market data directly from Yahoo Finance and, for optional Finnhub-backed features, Finnhub. The project does not operate a market-data proxy or bundle a market-data database.

The MIT license covers SquareStar's first-party code. It does not license third-party market data, provider services, trademarks, or publisher content.

## Providers used by the current source

| Provider | Use |
| --- | --- |
| `query1.finance.yahoo.com`, `query2.finance.yahoo.com` | Quotes, charts, search fallback, screeners, trends/overview |
| `fc.yahoo.com` | Yahoo cookie/crumb bootstrap |
| `finnhub.io` | Optional symbol search, company news/profile/metrics, quote fallback |

Requests originate from the user's computer.

## Finnhub

Users supply their own Finnhub API key. No maintainer-owned key is checked into or bundled with the project. The key is protected at rest with Windows DPAPI and sent only to Finnhub-backed requests.

Without a key, Finnhub-backed features are unavailable, but other Yahoo-backed paths can still work.

## Yahoo Finance

SquareStar is not an official Yahoo SDK, partner integration, or endorsed product. Yahoo controls its endpoints and can change fields, availability, rate limits, or request requirements independently of this repository.

Some Yahoo requests use an in-memory cookie/crumb flow. The project does not claim rights over Yahoo Finance data or Yahoo trademarks.

## Display behavior

SquareStar associates the displayed price with its provider timestamp and only labels quotes `Live`, `Realtime`, or `Delayed` when the provider provides that status.

Provider values can be incomplete, corrected, rate-limited, unavailable, or delayed in ways the application cannot reliably determine.

## Exports and links

Chart/data exports are local copies created at the user's request. Exporting does not change the terms that apply to provider-derived data.

Publisher/company pages are opened in the system browser; SquareStar does not fetch their full HTML in-process.

This notice describes the current source and is not legal advice. Users and distributors should review the current provider terms for their own use case.
