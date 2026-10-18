# Ghoti.io Color

A colour engine in C: it describes a colour space exactly, parses an ICC
profile, and transforms samples between two spaces. It is part of the
[Ghoti.io suite](https://github.com/Ghoti-io/suite).

It is deliberately not a colour *management* system. There is no display
profiling, no device calibration, no monitor LUT loading. The line is that
this library answers "what does this number mean, and what is it in that
space", not "what is this particular monitor doing today".

## What is implemented

**Phases 0 through 4 are done.** The library owns the colour description
(`GCOL_Color_Info` and the enums around it), the named-gamut and
transfer-convention tables, H.273 CICP → description
(`gcol_cicp_to_color_info`), matrix/TRC and LUT ICC parse
(`gcol_icc_write` / `gcol_icc_parse`), colour maths, relative-colorimetric
RGB↔RGB and gray↔RGB transforms (float output unbounded, as littleCMS's
is), CMYK LUT transforms with `cmyk_polarity`, PQ/HLG with
absolute scale, and SGI LogL16, LogLuv24 and LogLuv32 ↔ XYZ. `libs/image`
depends on this library for the description types. The plan is in the workspace at
`planning/color.md`.

| Phase | Content | Status |
| --- | --- | --- |
| scaffold | build, gates, result codes, version | **done** |
| 0 | the colour *description* moves here from `libs/image` | **done** |
| 1 | chromaticity to XYZ, chromatic adaptation, transfer curves | **done** |
| 2 | ICC parse and write, matrix/TRC profiles, transforms | **done** |
| 3 | LUT-based profiles, CMYK, ICC v4 tags | **done** |
| 4 | HDR: PQ, HLG, absolute luminance; LogLuv helpers | **done** |
| oracle | ArgyllCMS 3.3.0 + littleCMS 2.16 (`check-argyll`, `check-lcms`) | **done** |

A header appears under `include/ghoti.io/color/` in the commit that implements
it, so the directory listing is what exists rather than what is intended.

Perceptual and saturation rendering intents have no specification of their
own. When a profile carries `A2B0`/`B2A0` (perceptual) or `A2B2`/`B2A2`
(saturation), requesting that intent applies the table. Without the matching
table, the intent returns `GCOL_ERR_UNSUPPORTED` — this library does not
invent a gamut map. Absolute colorimetric on the LUT path uses the
colorimetric tables and scales PCS XYZ by media white / D50; on the matrix
path it skips Bradford adaptation so illuminant differences remain.
`GCOL_TRANSFORM_BPC` and `GCOL_TRANSFORM_TONE_MAP` are opt-in (black-point
compensation and BT.2390 PQ EETF). Colorimetric intents are what the
specification defines.

## Why not littleCMS

littleCMS is a reference implementation and this library does not wrap it,
for a reason that is easy to miss: **a shared dependency makes two readings
one measurement.** If `libs/color` *was* lcms2, every gate that consulted
lcms2 would agree by construction.

The CMM pins are therefore separate programs, not link dependencies:

| pin | command | judges |
|---|---|---|
| **ArgyllCMS 3.3.0** | `make check-argyll` | matrix/TRC and v2 LUT (independent of lcms2) |
| **littleCMS 2.16** | `make check-lcms` | the same, plus ICC v4 mAB / parametricCurveType |

`make oracle-build` builds both images. See
[documentation/design.md](documentation/design.md) and
`tests/data/icc/README.md`.

## Example

```c
#include <ghoti.io/color/color.h>
#include <stdio.h>

int main(void) {
  printf("Ghoti.io Color %s\n", gcol_version_string());
  printf("%s\n", gcol_result_string(GCOL_ERR_UNSUPPORTED));
  return 0;
}
```

## Building

`pkg-config` is the only way this library finds its dependency. There is no
sibling-checkout fallback; a dependency `pkg-config` cannot find is a hard
error naming the fix.

```
make                      # the shared and static libraries
make test                 # build and run the tests, and the gates
make test-asan            # the same under ASan and UBSan
make fuzz                 # fuzz_icc_parse for FUZZ_TIME seconds (default 60)
make check-argyll         # device→XYZ vs pinned ArgyllCMS 3.3.0
make check-lcms           # device→XYZ vs pinned littleCMS 2.16
make check-symbols        # every exported symbol carries the version token
make install PREFIX=...   # needs sudo for a system prefix
make help                 # every target
```

Fuzz corpora live under `tests/fuzz/corpus/<harness>/`. Only hand-built
`*.seed` files are tracked; units the fuzzer discovers stay local.
## Dependencies

`cutil`, for the allocator interface, and `security`, for the MD5 an ICC
profile identifier is defined as.

## Documentation

`make docs` runs Doxygen into `docs/`.
[documentation/design.md](documentation/design.md) says what this library is
for and what it refuses to be.

## Status

Pre-release, version 0.0.0, and the API is not stable. The suite is greenfield
and nothing is shipped anywhere, so an ABI break is cheap and is being spent
rather than worked around.

## License

LGPL-3.0-only. `COPYING` (GPL-3.0) and `COPYING.LESSER` (LGPL-3.0) both ship,
because LGPLv3 is written as a set of additional permissions on top of GPLv3.

Contributions are not being accepted at this time; see `CONTRIBUTING.md`.
