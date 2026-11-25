# Normal blend loop and zoomed-out OpenRaster groups

Same harness as `01-viewport.md` (`tests/bench_viewport`, release, `xvfb-run`).
Before numbers are the build from the viewport-cache commit. After numbers
include the tighter Normal span loop and the output-sized group composite.

| Measurement | Before | After |
| --- | ---: | ---: |
| `composite_rect` 1280×800, 4 layers, partial alpha | 28.900 ms | 21.210 ms |
| Zoomed-out OpenRaster group, 1600×1200 → 480×360 | 13.527 ms | 3.615 ms |
| Cold expose after `invalidate_all` (knock-on) | 7.355 ms | 5.891 ms |

The zoomed-out group used to run a full 1×1 `composite_rect` per screen pixel
(345600 layer samples, one tiny buffer allocation each). It now samples each
leaf once per output pixel into one buffer per group, and `test_ora` checks
that result against the old 1×1 composite. The Normal span loop matches the
previous per-pixel formula in `test_blend`.
