# Round 2 expose check

Release build (`clang -O3`), `tests/bench_viewport.cpp` via `xvfb-run`.
Same case as `01-viewport.md`: 960×600, two layers, a 400×280 selection,
1100×700 window. Three runs of the unchanged binary and three of this
branch, interleaved. Pixel checksum of the first frame is `1439375` on
every run, the same value as before.

CRT scanlines default off. The bench does not set a tool, so the tip
outline is not drawn. Neither path touches the composite cache.

| Measurement | Before | After |
| --- | ---: | ---: |
| Warm expose, selection active | 0.781 ms | 0.774 ms |
| Cold expose after `invalidate_all` | 5.722 ms | 5.744 ms |
| Premultiply a 1280×800 buffer | 1.822 ms | 1.823 ms |
| Normal blend, 1280×800, four layers | 19.228 ms | 19.239 ms |

The warm frame stays a cache blit. The new overlay and the tip outline
are extra cairo after that blit, and only when they are on.
