# Viewport cache and premultiplied blit

Release build (`g++ -O3`), `tests/bench_viewport.cpp` via `xvfb-run`.
The expose case is a 960×600 document, two layers, a 400×280 selection, inside
an 1100×700 window. Warm frames redraw that window with the composite cache
already filled (what a marching-ant tick does). Cold frames call
`invalidate_all` first. Pixel checksum of the first frame is the same before
and after (`1439375`), so the blit matches.

| Measurement | Before | After |
| --- | ---: | ---: |
| Warm expose, selection active | 1.604 ms | 0.801 ms |
| Cold expose after `invalidate_all` | 7.355 ms | 7.367 ms |
| Premultiply a 1280×800 buffer (reference) | 1.497 ms | 1.402 ms |

Ant ticks no longer copy or premultiply the viewport. Premultiply runs only
when the straight composite cache or the floating selection changes. A cold
invalidate still recomposites, so that time stays put.

Blend and zoomed-out OpenRaster numbers are in `02-blend-ora.md`.
