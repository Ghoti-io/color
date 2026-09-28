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
| `rgb_mft1.icc` | RGB AToB0 lut8Type — LUT parse / Argyll gate |
| `rgb_mab.icc` | RGB AToB1 lutAtoBType (v4) — mAB parse |
| `rgb_mab_para.icc` | RGB AToB1 mAB with parametricCurveType A curves |

## Oracles

Fixtures are generated from CIE arithmetic without asking a CMM
(`generate_lut.py` is structurally independent of littleCMS and ArgyllCMS).

**ArgyllCMS 3.3.0** is this library's own CMM pin:
`tools/oracle/containers/IMAGES`, built by `make oracle-build`, judged by
`make check-argyll`. Lookups go through `icclu` (handles the CMYK/RGB v2 LUT
fixtures); the version banner comes from `xicclu`. Independence from littleCMS
is structural — a separate CMM. ICC v4 (`rgb_mab*.icc`) is outside this pin:
Debian's Argyll 3.3 refuses v4.

**littleCMS** remains an incidental second reading via `libs/image`'s pinned
Pillow container. It is not linked into this library.
