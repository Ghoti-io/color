# Design

**Status:** design. Phases 0–4 (description, maths, matrix/TRC and LUT ICC
parse, CMYK polarity, relative- and absolute-colorimetric transforms, optional
BPC and BT.2390 PQ EETF, PQ/HLG absolute scale, LogLuv helpers) are
implemented.

This page says what this library is for, what it refuses to be, and why it
does not wrap the reference implementation. The phase plan lives outside this
repository, in the workspace at `planning/color.md`, because it is a note to
the sessions doing the work rather than to someone who found this library.

## What it is

Three things, in dependency order:

1. **A description of colour.** A gamut as four CIE chromaticities, a transfer
   function, whether samples are display- or scene-referred, and whether they
   are a measurement or a fraction. This part already exists in `libs/image`
   and moves here.
2. **An ICC reader and writer.** Profiles arrive from PNG `iCCP`, JPEG `APP2`,
   TIFF tag 34675 and BMP V5, and today no library in the suite can do
   anything with one but carry it.
3. **A transform.** Given two descriptions and a rendering intent, convert
   samples.

## What it is not

Not a colour management system. No display profiling, no device calibration,
no measurement hardware, no monitor LUT loading, no per-user display state.

The line: this library answers *what does this number mean, and what is it in
that space*. It does not answer *what is this particular monitor doing today*.
A caller who needs the second supplies a profile; this library will not go
looking for one, and in particular it never opens a file to find one - see
`libs/image`'s `icc_resolver`, which exists for exactly that reason.

## Why not littleCMS

littleCMS (lcms2, MIT) is the reference CMM. Pillow, GIMP, Firefox, Krita,
Ghostscript and Android all use it. It is not being wrapped, for three
reasons in descending order of weight.

**It is already the oracle.** This library pins littleCMS 2.16 in
`tools/oracle/containers/lcms2/` (`make check-lcms`) and ArgyllCMS 3.3.0
beside it (`make check-argyll`). A `libs/color` that *was* lcms2 would make
the littleCMS gate compare lcms2 against lcms2, and it would then agree by
construction — the gate would stop being able to fail. A shared dependency
makes two readings into one measurement. Argyll is the independent second
CMM for matrix and v2 LUT; littleCMS is what can judge ICC v4.

**It would be the suite's first third-party dependency.** `libs/image` links
`-lstdc++ -lm` and nothing else; every other dependency resolves through
pkg-config to a sibling, and `libs/compress` implements deflate rather than
linking zlib. Whether that property is worth keeping is a suite-level
decision, and it was made deliberately rather than by drift.

**The tail is what wrapping buys, and the tail is not what is needed.**
ICC v4 LUT pipelines, spot colour and the perceptual reference medium gamut
are real work that lcms2 has done. None of it is needed to make the existing
corpus discriminate between a correct transform and one that does nothing.

### What that costs

Two things, stated rather than glossed.

The v4 tail above is genuine work and will take phases.

**Perceptual and saturation intents have no specification of their own.** A
profile may still carry A2B0/B2A0 or A2B2/B2A2 tables; applying those bytes
under the matching intent is honouring the profile, not inventing a mapping.
Without those tables, the intent returns `GCOL_ERR_UNSUPPORTED`. Inventing a
silent default gamut map would be the wrong kind of helpful. Colorimetric
intents do not have this problem: they are defined, and a disagreement there
is a bug in one of the two implementations. Absolute colorimetric on the LUT
path uses the colorimetric tables and scales by media white / D50.

## Why it is not part of `libs/image`

Because `font` depends on `cutil` alone and `cjelly` depends on `cutil`,
`image` and `model`. Both need to act on colour - COLRv1 palettes in one,
material colours and swapchain transfer in the other - and `font` reaching
through `libs/image` to ask what sRGB means would be the wrong graph.

`libs/image` keeps its stance: it *describes* colour and never transforms it.
Gaining this library as a dependency does not change that, and does not make
it open a file.

## The seam

> `image` reads bytes and fills fields. `color` interprets standards and
> computes.

Parsing a PNG `cHRM` chunk is PNG's business. Knowing that H.273 transfer
characteristic 16 is PQ, that PQ is display-referred and absolute, and what
its EOTF is, is not: those code points appear in AVIF, HEIF and MP4 as well,
and the library that knows them should not be a PNG reader.

## Result vocabulary

CONVENTIONS.md section 5's list, with nothing added. A colour conversion that
cannot be performed is not a new kind of failure: a profile this library
cannot yet interpret is `GCOL_ERR_UNSUPPORTED`, and a profile whose bytes
contradict its own tag table is `GCOL_ERR_CORRUPT`.

There is no `GCOL_Limits` yet. Section 5 says every parser takes one, and the
ICC parser will - a tag count, a curve sample count and a LUT grid size are
all unbounded in the input. The struct arrives in the commit that enforces its
first field, because a cap nothing checks is not a promise.
