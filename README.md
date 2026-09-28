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
transfer-convention tables, matrix/TRC and LUT ICC parse
(`gcol_icc_write` / `gcol_icc_parse`), colour maths, relative-colorimetric
RGB↔RGB transforms, CMYK LUT transforms with `cmyk_polarity`, PQ/HLG with
absolute scale, and LogLuv32↔XYZ helpers. `libs/image` depends on this
library for the description types. The plan is in the workspace at
`planning/color.md`.

| Phase | Content | Status |
| --- | --- | --- |
| scaffold | build, gates, result codes, version | **done** |
| 0 | the colour *description* moves here from `libs/image` | **done** |
| 1 | chromaticity to XYZ, chromatic adaptation, transfer curves | **done** |
| 2 | ICC parse and write, matrix/TRC profiles, transforms | **done** |
| 3 | LUT-based profiles, CMYK, ICC v4 tags | **done** |
| 4 | HDR: PQ, HLG, absolute luminance; LogLuv helpers | **done** |

A header appears under `include/ghoti.io/color/` in the commit that implements
it, so the directory listing is what exists rather than what is intended.

Perceptual rendering intent is **not** implemented: it has no specification,
and requesting it returns `GCOL_ERR_UNSUPPORTED`. Colorimetric intents are
what this library transforms under.

## Why not littleCMS

littleCMS is the reference implementation and this library does not wrap it,
for a reason that is easy to miss: **lcms2 is already the suite's oracle.**
`libs/image`'s ICC corpus is checked against littleCMS 2.16 in a pinned
container, so a `libs/color` that *was* lcms2 would make that check compare
lcms2 against lcms2 and agree by construction. A shared dependency makes two
readings one measurement.

lcms2 stays where it is: an oracle, pinned, and never linked. See
[documentation/design.md](documentation/design.md).

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
make check-symbols        # every exported symbol carries the version token
make install PREFIX=...   # needs sudo for a system prefix
make help                 # every target
```

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
