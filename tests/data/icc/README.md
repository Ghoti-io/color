# ICC fixtures for libs/color

Copies of the six profiles under `libs/image/tests/data/icc/`. Regenerate
there with `generate.py`; copy the `.icc` files here when they change.

| profile | role |
|---|---|
| `srgb_g22.icc` | sRGB primaries, gamma 2.2 |
| `adobergb_g22.icc` | Adobe RGB, gamma 2.2 |
| `displayp3_g22.icc` | Display P3, gamma 2.2 |
| `srgb_sampled_trc.icc` | sRGB primaries, 1024-point curve |
| `gray_g22.icc` | grayscale, gamma 2.2 |
| `swap_rg.icc` | sRGB red/green exchanged, linear — the phase-2 gate |
