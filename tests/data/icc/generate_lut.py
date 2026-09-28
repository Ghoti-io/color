#!/usr/bin/env python3
"""
Write minimal LUT / CMYK ICC fixtures for libs/color phase 3.

Deterministic: zeroed date, no profile ID. Colorants and CLUT values are
computed here from first principles so the corpus is not graded by the
library's own LUT code.

Profiles
--------
* ``cmyk_mft2.icc`` — CMYK AToB1 lut16Type (mft2), 2-point CLUT, XYZ PCS,
  plus a distinct AToB0 (XYZ×0.5) so perceptual and relative disagree, and
  BToA1 for destination CMYK. Device (C,M,Y,K) in reflection polarity
  (0=no ink) maps under A2B1 to absolute XYZ under D50 via a simple
  subtractive model. CLUT samples use the ICC XYZ PCS encoding (u1Fixed15
  for 16-bit). The polarity gate inverts storage bytes under
  GCOL_CMYK_POLARITY_INK before the LUT sees them.

* ``rgb_mft1.icc`` — RGB AToB0 lut8Type (mft1), 2-point CLUT approximating
  sRGB→XYZ D50. CLUT samples use the 8-bit XYZ PCS encoding (code/128).

* ``rgb_mab.icc`` — RGB AToB1 lutAtoBType (mAB ), B-curves only (identity),
  so v4-style tags parse. Outside ArgyllCMS 3.3's reach (no ICC v4).

* ``rgb_mab_para.icc`` — RGB AToB1 mAB with parametricCurveType A curves
  (function 0, gamma 2.2) and identity B curves; no CLUT. Exercises the
  parametric path without needing a CLUT.

Independence of oracles
-----------------------
These bytes are generated without asking littleCMS or ArgyllCMS. Optional
verification against those CMMs is ``make check-argyll`` (Argyll) and
image's Pillow/lcms2 container.
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


def encode_xyz_u8(X: float, Y: float, Z: float) -> bytes:
    """8-bit XYZ PCS: code = round(component * 128), clamped to 0..255."""
    out = bytearray()
    for v in (X, Y, Z):
        code = int(round(v * 128.0))
        if code < 0:
            code = 0
        if code > 255:
            code = 255
        out.append(code)
    return bytes(out)


def encode_xyz_u16(X: float, Y: float, Z: float) -> bytes:
    """16-bit XYZ PCS: u1Fixed15Number = round(component * 32768)."""
    out = bytearray()
    for v in (X, Y, Z):
        code = int(round(v * 32768.0))
        if code < 0:
            code = 0
        if code > 65535:
            code = 65535
        out += struct.pack(">H", code)
    return bytes(out)


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


def rgb_to_xyz_abs(r, g, b):
    """sRGB linear → absolute XYZ under D50 (Bradford-adapted matrix)."""
    rx, ry, rz = 0.43607, 0.22249, 0.01392
    gx, gy, gz = 0.38515, 0.71687, 0.09708
    bx, by, bz = 0.14307, 0.06061, 0.71410
    X = r * rx + g * gx + b * bx
    Y = r * ry + g * gy + b * by
    Z = r * rz + g * gz + b * bz
    return (X, Y, Z)


def rgb_to_cmyk(r, g, b):
    """Simple undercolour removal: K = min(CMY), then scale CMY."""
    c = 1.0 - max(0.0, min(1.0, r))
    m = 1.0 - max(0.0, min(1.0, g))
    y = 1.0 - max(0.0, min(1.0, b))
    k = min(c, m, y)
    if k >= 1.0 - 1e-12:
        return 0.0, 0.0, 0.0, 1.0
    if k > 0.0:
        c = (c - k) / (1.0 - k)
        m = (m - k) / (1.0 - k)
        y = (y - k) / (1.0 - k)
    return c, m, y, k


def xyz_to_rgb_abs(X, Y, Z):
    """Inverse of rgb_to_xyz_abs (D50-adapted sRGB matrix)."""
    # Inverse of columns (rx,gx,bx), (ry,gy,by), (rz,gz,bz).
    # Hand-computed adjugate / det for the published matrix.
    rx, ry, rz = 0.43607, 0.22249, 0.01392
    gx, gy, gz = 0.38515, 0.71687, 0.09708
    bx, by, bz = 0.14307, 0.06061, 0.71410
    # 3x3 inverse
    det = (
        rx * (gy * bz - gz * by)
        - gx * (ry * bz - rz * by)
        + bx * (ry * gz - rz * gy)
    )
    inv = [
        (gy * bz - gz * by) / det,
        (bx * gz - gx * bz) / det,
        (gx * by - bx * gy) / det,
        (rz * by - ry * bz) / det,
        (rx * bz - bx * rz) / det,
        (bx * ry - rx * by) / det,
        (ry * gz - rz * gy) / det,
        (gx * rz - rx * gz) / det,
        (rx * gy - gx * ry) / det,
    ]
    r = inv[0] * X + inv[1] * Y + inv[2] * Z
    g = inv[3] * X + inv[4] * Y + inv[5] * Z
    b = inv[6] * X + inv[7] * Y + inv[8] * Z
    return r, g, b


def encode_cmyk_u16(c, m, y, k) -> bytes:
    out = bytearray()
    for v in (c, m, y, k):
        code = int(round(max(0.0, min(1.0, v)) * 65535.0))
        out += struct.pack(">H", code)
    return bytes(out)


def tag_mft2_cmyk_b2a():
    """lut16Type BToA: 3 in (XYZ PCS), 4 out (CMYK), grid=3.

    Grid=3 so the D50 white point (PCS code ~0.5) sits on a sample rather
    than halfway between black and a >1.0 corner on a 2-point grid.
    """
    grid = 3
    in_ch, out_ch = 3, 4
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
    scale = 65535.0 / 32768.0
    clut = bytearray()
    for xi in range(grid):
        for yi in range(grid):
            for zi in range(grid):
                X = (xi / (grid - 1)) * scale
                Y = (yi / (grid - 1)) * scale
                Z = (zi / (grid - 1)) * scale
                r, g, b = xyz_to_rgb_abs(X, Y, Z)
                clut += encode_cmyk_u16(*rgb_to_cmyk(r, g, b))
    tables_out = identity_u16(out_entries) * out_ch
    return header + tables_in + bytes(clut) + tables_out


def tag_mft2_cmyk():
    """lut16Type: 4 in, 3 out, grid=2, identity tables, subtractive CLUT."""
    grid = 2
    in_ch, out_ch = 4, 3
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
    for ci in range(grid):
        for mi in range(grid):
            for yi in range(grid):
                for ki in range(grid):
                    c = ci / (grid - 1)
                    m = mi / (grid - 1)
                    y = yi / (grid - 1)
                    k = ki / (grid - 1)
                    r, g, b = subtractive_rgb(c, m, y, k)
                    clut += encode_xyz_u16(*rgb_to_xyz_abs(r, g, b))
    tables_out = identity_u16(out_entries) * out_ch
    return header + tables_in + bytes(clut) + tables_out


def tag_mft2_cmyk_perceptual():
    """A2B0: same structure as A2B1 but XYZ scaled by 0.5 so intents disagree."""
    grid = 2
    in_ch, out_ch = 4, 3
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
    for ci in range(grid):
        for mi in range(grid):
            for yi in range(grid):
                for ki in range(grid):
                    c = ci / (grid - 1)
                    m = mi / (grid - 1)
                    y = yi / (grid - 1)
                    k = ki / (grid - 1)
                    r, g, b = subtractive_rgb(c, m, y, k)
                    X, Y, Z = rgb_to_xyz_abs(r, g, b)
                    clut += encode_xyz_u16(0.5 * X, 0.5 * Y, 0.5 * Z)
    tables_out = identity_u16(out_entries) * out_ch
    return header + tables_in + bytes(clut) + tables_out


def tag_mft1_rgb_identity():
    """lut8Type: 3 in, 3 out, grid=2, sRGB→XYZ CLUT."""
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
                clut += encode_xyz_u8(*rgb_to_xyz_abs(r, g, b))
    tables_out = identity_u8() * out_ch
    return header + tables_in + bytes(clut) + tables_out


def tag_curv_identity() -> bytes:
    return b"curv" + b"\0" * 4 + struct.pack(">I", 0)


def tag_para_gamma(g: float) -> bytes:
    """parametricCurveType function 0: Y = X^g."""
    return b"para" + b"\0" * 4 + struct.pack(">HH", 0, 0) + s15f16(g)


def tag_mab_b_only():
    """lutAtoBType with only B curves (identity) — enough to parse."""
    b0 = tag_curv_identity()
    b1 = tag_curv_identity()
    b2 = tag_curv_identity()
    off_b = 32
    payload = bytearray(32)
    payload[0:4] = b"mAB "
    payload[8] = 3  # input
    payload[9] = 3  # output
    payload[12:16] = struct.pack(">I", off_b)  # B
    body = b0 + b1 + b2
    return bytes(payload) + body


def tag_mab_para_a():
    """lutAtoBType: parametric A curves (γ=2.2) + identity B; no CLUT."""
    a = tag_para_gamma(2.2)
    assert len(a) == 16
    b = tag_curv_identity()
    assert len(b) == 12
    # Layout: header 32, then 3×A (16 each), then 3×B (12 each).
    off_a = 32
    off_b = off_a + 3 * 16
    payload = bytearray(32)
    payload[0:4] = b"mAB "
    payload[8] = 3
    payload[9] = 3
    payload[12:16] = struct.pack(">I", off_b)  # B
    payload[28:32] = struct.pack(">I", off_a)  # A
    body = a * 3 + b * 3
    return bytes(payload) + body


def main() -> None:
    cmyk = build_profile(
        b"CMYK",
        [
            (b"desc", tag_desc("Ghoti test CMYK mft2")),
            (b"cprt", tag_text("No copyright, use freely.")),
            (b"wtpt", tag_xyz(D50_XYZ)),
            (b"A2B1", tag_mft2_cmyk()),
            (b"A2B0", tag_mft2_cmyk_perceptual()),
            (b"B2A1", tag_mft2_cmyk_b2a()),
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

    rgb_para = build_profile(
        b"RGB ",
        [
            (b"desc", tag_desc("Ghoti test RGB mAB parametric")),
            (b"cprt", tag_text("No copyright, use freely.")),
            (b"wtpt", tag_xyz(D50_XYZ)),
            (b"A2B1", tag_mab_para_a()),
        ],
        "Ghoti test RGB mAB parametric",
        version=0x04400000,
        profile_class=b"mntr",
    )
    with open(os.path.join(OUT, "rgb_mab_para.icc"), "wb") as f:
        f.write(rgb_para)

    print("wrote cmyk_mft2.icc (%d bytes)" % len(cmyk))
    print("wrote rgb_mft1.icc (%d bytes)" % len(rgb_mft1))
    print("wrote rgb_mab.icc (%d bytes)" % len(rgb_mab))
    print("wrote rgb_mab_para.icc (%d bytes)" % len(rgb_para))


if __name__ == "__main__":
    main()
