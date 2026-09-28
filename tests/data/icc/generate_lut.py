#!/usr/bin/env python3
"""
Write minimal LUT / CMYK ICC fixtures for libs/color phase 3.

Deterministic: zeroed date, no profile ID. Colorants and CLUT values are
computed here from first principles so the corpus is not graded by the
library's own LUT code.

Profiles
--------
* ``cmyk_mft2.icc`` — CMYK AToB1 lut16Type (mft2), 2-point CLUT, XYZ PCS.
  Device (C,M,Y,K) in reflection polarity (0=no ink) maps to relative XYZ
  fractions of D50 via a simple subtractive model. The polarity gate inverts
  storage bytes under GCOL_CMYK_POLARITY_INK before the LUT sees them.

* ``rgb_mft1.icc`` — RGB AToB0 lut8Type (mft1), 2-point CLUT approximating
  identity under D50, so the profile *reads* as a LUT profile.

* ``rgb_mab.icc`` — RGB AToB1 lutAtoBType (mAB ), B-curves only (identity),
  so v4-style tags parse.

Independence of oracles
-----------------------
These bytes are generated without asking littleCMS or ArgyllCMS. Optional
verification against lcms2 (via Pillow) is a separate script; ArgyllCMS is
not pinned in this environment (see tests/data/icc/README.md).
"""
from __future__ import annotations

import os
import struct

OUT = os.path.dirname(os.path.abspath(__file__))
D50_XYZ = (0.9642, 1.0000, 0.8249)


def s15f16(v: float) -> bytes:
    return struct.pack(">i", int(round(v * 65536.0)))


def pad4(n: int) -> int:
    return (4 - (n % 4)) % 4


def tag_desc(text: str) -> bytes:
    ascii_bytes = text.encode("ascii") + b"\0"
    return (
        b"desc"
        + b"\0" * 4
        + struct.pack(">I", len(ascii_bytes))
        + ascii_bytes
        + struct.pack(">I", 0)
        + struct.pack(">I", 0)
        + struct.pack(">H", 0)
        + struct.pack(">B", 0)
        + b"\0" * 67
    )


def tag_text(text: str) -> bytes:
    return b"text" + b"\0" * 4 + text.encode("ascii") + b"\0"


def tag_xyz(triple) -> bytes:
    return b"XYZ " + b"\0" * 4 + b"".join(s15f16(v) for v in triple)


def identity_u16(n: int = 2) -> bytes:
    """n samples from 0 to 65535 inclusive."""
    if n < 2:
        raise ValueError(n)
    out = []
    for i in range(n):
        out.append(int(round(i * 65535.0 / (n - 1))))
    return b"".join(struct.pack(">H", v) for v in out)


def identity_u8() -> bytes:
    return bytes(range(256))


def build_profile(space: bytes, tags, desc: str, version: int = 0x02100000,
                  profile_class: bytes = b"prtr") -> bytes:
    tags = list(tags)
    count = len(tags)
    table_size = 4 + count * 12
    body = bytearray()
    entries = []
    for sig, payload in tags:
        entries.append((sig, 128 + table_size + len(body), len(payload)))
        body += payload
        body += b"\0" * pad4(len(body))
    total = 128 + table_size + len(body)

    h = bytearray(128)
    h[0:4] = struct.pack(">I", total)
    h[8:12] = struct.pack(">I", version)
    h[12:16] = profile_class
    h[16:20] = space
    h[20:24] = b"XYZ "
    h[36:40] = b"acsp"
    h[64:68] = struct.pack(">I", 1)  # relative colorimetric
    h[68:80] = b"".join(s15f16(v) for v in D50_XYZ)

    out = bytes(h) + struct.pack(">I", count)
    for sig, off, size in entries:
        out += sig + struct.pack(">II", off, size)
    out += bytes(body)
    assert len(out) == total
    assert desc
    return out


def subtractive_rgb(c, m, y, k):
    """Simple CMYK→RGB under reflection polarity."""
    r = (1.0 - c) * (1.0 - k)
    g = (1.0 - m) * (1.0 - k)
    b = (1.0 - y) * (1.0 - k)
    return r, g, b


def rgb_to_xyz_frac(r, g, b):
    """sRGB linear → XYZ, returned as fractions of D50 (so white→1,1,1)."""
    # Columns of Bradford-adapted sRGB→XYZ D50 from generate.py / publication.
    rx, ry, rz = 0.43607, 0.22249, 0.01392
    gx, gy, gz = 0.38515, 0.71687, 0.09708
    bx, by, bz = 0.14307, 0.06061, 0.71410
    X = r * rx + g * gx + b * bx
    Y = r * ry + g * gy + b * by
    Z = r * rz + g * gz + b * bz
    return (X / D50_XYZ[0], Y / D50_XYZ[1], Z / D50_XYZ[2])


def tag_mft2_cmyk():
    """lut16Type: 4 in, 3 out, grid=2, identity tables, subtractive CLUT."""
    grid = 2
    in_ch, out_ch = 4, 3
    # identity matrix
    mat = b"".join(s15f16(1.0 if i % 4 == 0 else 0.0) for i in range(9))
    in_entries = 2
    out_entries = 2
    header = (
        b"mft2"
        + b"\0" * 4
        + bytes([in_ch, out_ch, grid, 0])
        + mat
        + struct.pack(">HH", in_entries, out_entries)
    )
    tables_in = identity_u16(in_entries) * in_ch
    clut = bytearray()
    # CLUT order: nested C, M, Y, K from 0..grid-1
    for ci in range(grid):
        for mi in range(grid):
            for yi in range(grid):
                for ki in range(grid):
                    c = ci / (grid - 1)
                    m = mi / (grid - 1)
                    y = yi / (grid - 1)
                    k = ki / (grid - 1)
                    r, g, b = subtractive_rgb(c, m, y, k)
                    xf, yf, zf = rgb_to_xyz_frac(r, g, b)
                    for v in (xf, yf, zf):
                        v = max(0.0, min(1.0, v))
                        clut += struct.pack(
                            ">H", int(round(v * 65535.0)))
    tables_out = identity_u16(out_entries) * out_ch
    return header + tables_in + bytes(clut) + tables_out


def tag_mft1_rgb_identity():
    """lut8Type: 3 in, 3 out, grid=2, near-identity CLUT."""
    grid = 2
    in_ch, out_ch = 3, 3
    mat = b"".join(s15f16(1.0 if i % 4 == 0 else 0.0) for i in range(9))
    header = b"mft1" + b"\0" * 4 + bytes([in_ch, out_ch, grid, 0]) + mat
    tables_in = identity_u8() * in_ch
    clut = bytearray()
    for ri in range(grid):
        for gi in range(grid):
            for bi in range(grid):
                r = ri / (grid - 1)
                g = gi / (grid - 1)
                b = bi / (grid - 1)
                xf, yf, zf = rgb_to_xyz_frac(r, g, b)
                for v in (xf, yf, zf):
                    clut.append(int(round(max(0.0, min(1.0, v)) * 255.0)))
    tables_out = identity_u8() * out_ch
    return header + tables_in + bytes(clut) + tables_out


def tag_curv_identity() -> bytes:
    return b"curv" + b"\0" * 4 + struct.pack(">I", 0)


def tag_mab_b_only():
    """lutAtoBType with only B curves (identity) — enough to parse."""
    # Layout: header 32 bytes, then three curv tags for B.
    b0 = tag_curv_identity()
    b1 = tag_curv_identity()
    b2 = tag_curv_identity()
    # offsets relative to tag start
    off_b = 32
    payload = bytearray(32)
    payload[0:4] = b"mAB "
    payload[8] = 3  # input
    payload[9] = 3  # output
    payload[12:16] = struct.pack(">I", off_b)  # B
    # matrix, M, CLUT, A all zero
    body = b0 + b1 + b2
    # pad curves to 4-byte alignment already (12 bytes each)
    return bytes(payload) + body


def main() -> None:
    cmyk = build_profile(
        b"CMYK",
        [
            (b"desc", tag_desc("Ghoti test CMYK mft2")),
            (b"cprt", tag_text("No copyright, use freely.")),
            (b"wtpt", tag_xyz(D50_XYZ)),
            (b"A2B1", tag_mft2_cmyk()),
        ],
        "Ghoti test CMYK mft2",
        profile_class=b"prtr",
    )
    with open(os.path.join(OUT, "cmyk_mft2.icc"), "wb") as f:
        f.write(cmyk)

    rgb_mft1 = build_profile(
        b"RGB ",
        [
            (b"desc", tag_desc("Ghoti test RGB mft1")),
            (b"cprt", tag_text("No copyright, use freely.")),
            (b"wtpt", tag_xyz(D50_XYZ)),
            (b"A2B0", tag_mft1_rgb_identity()),
        ],
        "Ghoti test RGB mft1",
        profile_class=b"mntr",
    )
    with open(os.path.join(OUT, "rgb_mft1.icc"), "wb") as f:
        f.write(rgb_mft1)

    rgb_mab = build_profile(
        b"RGB ",
        [
            (b"desc", tag_desc("Ghoti test RGB mAB")),
            (b"cprt", tag_text("No copyright, use freely.")),
            (b"wtpt", tag_xyz(D50_XYZ)),
            (b"A2B1", tag_mab_b_only()),
        ],
        "Ghoti test RGB mAB",
        version=0x04400000,
        profile_class=b"mntr",
    )
    with open(os.path.join(OUT, "rgb_mab.icc"), "wb") as f:
        f.write(rgb_mab)

    print("wrote cmyk_mft2.icc (%d bytes)" % len(cmyk))
    print("wrote rgb_mft1.icc (%d bytes)" % len(rgb_mft1))
    print("wrote rgb_mab.icc (%d bytes)" % len(rgb_mab))


if __name__ == "__main__":
    main()
