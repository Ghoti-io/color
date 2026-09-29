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
| `cmyk_mft2.icc` | CMYK AToB1/0/2 + BToA1 (grid=5) — polarity / intent / round-trip |
| `cmyk_abs.icc` | CMYK AToB1 with non-D50 wtpt + bkpt — absolute intent / BPC |
| `rgb_mft1.icc` | RGB AToB0 lut8Type — LUT parse / Argyll gate |
| `rgb_lab_mft2.icc` | RGB Lab PCS AToB1 + BToA1 — Lab round trip |
| `rgb_mab.icc` | RGB AToB1 lutAtoBType (v4) — mAB parse |
| `rgb_mab_para.icc` | RGB AToB1 mAB with parametricCurveType A curves |

## Oracles

Fixtures are generated from CIE arithmetic without asking a CMM
(`generate_lut.py` is structurally independent of littleCMS and ArgyllCMS).

Two CMM pins live under `tools/oracle/containers/`. Neither is linked into
the library; `make oracle-build` builds both images.

**ArgyllCMS 3.3.0** — `make check-argyll`. Lookups go through `icclu`
(handles the CMYK/RGB v2 LUT fixtures); the version banner comes from
`xicclu`. Independence from littleCMS is structural — a separate CMM. ICC
v4 (`rgb_mab*.icc`) is outside this pin: Debian's Argyll 3.3 refuses v4.

**littleCMS 2.16** — `make check-lcms`. Lookups go through
`lcms_icc_lookup` (built into the image against liblcms2). This is the pin
that can judge ICC v4 mAB / parametricCurveType. Matrix and CMYK cases are
checked here as well. `rgb_mft1` is Argyll-only: the two CMMs disagree on
that 8-bit LUT by a few parts in 1e3, and this library matches Argyll.
