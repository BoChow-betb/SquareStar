# SquareStar de-optimization v9

This revision intentionally removes optimizations whose maintenance/state cost
was larger than their practical benefit for SquareStar's measured workload.

## Removed

- Periodic 5s/20s idle memory-trim scheduler in the main loop.
- Direct use of Dear ImGui / ImPlot internal compaction APIs.
- SquareStar-specific DX11 backend vertex/index-buffer compaction hook.
- Dynamic Windows LFH `HeapOptimizeResources` process-memory trimming.
- Idle audio-device/cache teardown driven by the memory-trim scheduler.

Normal ownership-based cleanup still runs when contexts/modes are actually
released, and audio still stops/cleans up through its normal lifecycle.

## Simplified policy

Chart LOD remains available as a safety valve, but it is bypassed for histories
with 4,096 points or fewer. Current provider ranges normally contain only
hundreds of points, where downsampling is unnecessary.

## Kept on purpose

- Event-driven main loop.
- Binary search for sorted time ranges.
- Bounded stock tabs and bounded caches.
- Network request deduplication / single-flight behavior.
- LOD implementation for genuinely large datasets.
- Explicit resource release at real lifecycle boundaries.

Principle: optimize measured problems, not Task Manager cosmetics.
