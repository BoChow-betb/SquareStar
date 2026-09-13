# Security threat model

This document describes controls present in the current source tree. It does not claim
that SquareStar is formally audited or free of vulnerabilities.

## Sensitive assets and trust boundaries

| Asset / input | Main risk | Current boundary |
| --- | --- | --- |
| Finnhub API key | Credential disclosure or stale-key races | Settings input, DPAPI-protected key store, Finnhub requests |
| Saved private state | Plaintext disclosure / malformed state | Versioned config codec, DPAPI-protected private payload |
| Provider JSON | Malformed/unbounded input | HTTPS transport, response-size limits, bounded yyjson parsing helpers |
| Public screener cache | Corrupt/stale local input | Separate bounded/versioned cache format |
| Provider/request URLs | Unexpected network destination | API allowlist plus hard-coded Yahoo bootstrap path |
| Article/company links | Browser navigation to unsafe authority syntax | External HTTPS URL policy, then system browser |
| Export paths | Local path/output correctness | User-selected/local resolved paths and bounded export jobs |

## Implemented controls

- The Finnhub key is protected at rest with Windows DPAPI and is not pre-filled back
  into the Settings text box.
- Key changes use revision-aware persistence; late provider results can be rejected
  after a key replacement/clear.
- Credential-bearing Finnhub URLs are not used as durable quote single-flight keys;
  request strings are cleared/reset after use where the current network runtime owns
  them.
- Shared provider API URLs are limited to HTTPS hosts
  `query1.finance.yahoo.com`, `query2.finance.yahoo.com`, and `finnhub.io`.
- Yahoo's `fc.yahoo.com` request is a separate hard-coded bootstrap step for the Yahoo
  cookie/crumb flow rather than a general allowlisted URL supplied by UI/provider
  payload data.
- libcurl peer and host verification are enabled for provider HTTP.
- External browser links reject credentials, custom ports, literal addresses,
  localhost-style names, control characters, and non-HTTPS schemes.
- Provider payload parsers and persisted configuration use explicit size/count bounds.
- `state.json` writes go through a coordinated writer/temporary-file replacement path;
  saved-state format is versioned and invalid/undecryptable state is rejected.
- Public screener cache data is stored separately from watchlist/search-history private
  state.
- Local diagnostics sanitize fields and are not uploaded by the current source.

## Out of scope / residual risk

- DPAPI does not protect against malware already running as the same Windows user.
- Third-party providers and browser-opened websites are separate trust domains.
- Market-data correctness/latency is not a security guarantee made by SquareStar.
- A source audit does not replace Windows binary testing, dependency review, or a
  professional security assessment.

Report suspected vulnerabilities using the private guidance in `SECURITY.md`.
