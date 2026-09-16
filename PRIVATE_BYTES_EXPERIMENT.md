# Private Bytes experiment v1

Goal: reduce retained Windows Private Bytes without changing SquareStar's
rendering, worker counts, network connection reuse, chart data precision, or UI behavior.

Changes:
1. Enable Windows Segment Heap in `src/app.manifest`.
2. On Windows, give each yyjson parse a short-lived private heap.
   The heap is destroyed with the parsed document, so parser pages are
   decommitted/released rather than being left in the long-lived process heap.
3. Non-Windows builds keep yyjson's existing allocator behavior.

Intentionally unchanged:
- HTTP worker count/concurrency
- background/search worker count
- libcurl connection reuse
- D3D11/ImGui buffers
- audio-device caching
- stock/chart data precision
- font/render behavior

Validation performed here:
- Release portable build completed.
- 29/29 portable tests passed.

Not measurable in this environment:
- Windows Private Bytes delta
- Windows-only compile/runtime validation

Recommended Windows comparison:
1. Build the original and this experiment with the same compiler/configuration.
2. Run `SquareStar.exe --benchmark-memory --benchmark-output <dir>`.
3. Compare `memory-layers.csv`, especially A-H.
4. Also compare normal fresh-launch idle, one-stock idle, and four-stock active
   using the same interaction sequence.

Treat `<10 MB active` as an experimental target, not a guaranteed outcome.
