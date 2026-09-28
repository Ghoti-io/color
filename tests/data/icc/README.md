# ICC fixtures for libs/color

Matrix/TRC profiles were copied from `libs/image/tests/data/icc/` (regenerate
there with `generate.py`). LUT/CMYK fixtures are produced here by
`generate_lut.py`.

| profile | role |
|---|---|
| `srgb_g22.icc` | sRGB primaries, gamma 2.2 |
| `adobergb_g22.icc` | Adobe RGB, gamma 2.2 |
| `displayp3_g22.icc` | Display P3, gamma 2.2 |
| `srgb_sampled_trc.icc` | sRGB primaries, 1024-point curve |
| `gray_g22.icc` | grayscale, gamma 2.2 |
| `swap_rg.icc` | sRGB red/green exchanged, linear — phase-2 gate |
| `cmyk_mft2.icc` | CMYK AToB1 lut16Type — phase-3 polarity gate |
| `rgb_mft1.icc` | RGB AToB0 lut8Type — LUT parse |
| `rgb_mab.icc` | RGB AToB1 lutAtoBType (v4) — mAB parse |

## Oracles

Fixtures are generated from CIE arithmetic without asking a CMM
(`generate_lut.py` is structurally independent of littleCMS and ArgyllCMS).

**littleCMS** remains available as an incidental oracle via
`libs/image`'s pinned Pillow container (`verify_icc.py` / `oracle-exec`), and
can be pointed at these bytes the same way. It is not wired into `libs/color`'s
`make test` for phase 3.

**ArgyllCMS** is not pinned in this environment. The Debian package exists on
the archive (`argyll`) but no container image or VERSION pin was added: the
handoff allows documenting that gap rather than blocking the phase. When a
second reading for the perceptual tail is needed, pin Argyll beside the
Pillow image and state whether its independence is structural or incidental.
