#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Color.
#
# Ghoti.io Color is free software: you can redistribute it and/or modify
# it under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
"""Compare this library's device→PCS XYZ against littleCMS 2.

littleCMS is the CMM that can judge ICC v4 (mAB / parametricCurveType).
ArgyllCMS remains the independent second reading for matrix and v2 LUT.

    python3 tools/oracle/verify_lcms.py \\
        build/.../oracle/gcol_icc_lookup
"""

from __future__ import annotations

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
ICC = os.path.join(ROOT, "tests", "data", "icc")

# Absolute XYZ agreement. Matrix path is tight; coarse CLUTs and the
# synthetic mAB fixtures set the shared bound.
TOL = 1e-3

# Includes rgb_mab*.icc — the reason this pin exists beside Argyll.
# rgb_mft1.icc is deliberately absent: on that 8-bit LUT fixture littleCMS
# and Argyll disagree by a few parts in 1e3, and this library matches Argyll
# (check-argyll). A shared bound that passed both would hide that split.
CASES = [
    ("swap_rg.icc", (1.0, 0.0, 0.0)),
    ("swap_rg.icc", (0.0, 1.0, 0.0)),
    ("swap_rg.icc", (1.0, 1.0, 1.0)),
    ("srgb_g22.icc", (1.0, 0.0, 0.0)),
    ("srgb_g22.icc", (1.0, 1.0, 1.0)),
    ("cmyk_mft2.icc", (0.0, 0.0, 0.0, 0.0)),
    ("cmyk_mft2.icc", (1.0, 0.0, 0.0, 0.0)),
    ("cmyk_mft2.icc", (0.0, 0.0, 0.0, 1.0)),
    ("rgb_mab.icc", (1.0, 0.0, 0.0)),
    ("rgb_mab.icc", (0.5, 0.0, 0.0)),
    ("rgb_mab.icc", (1.0, 1.0, 1.0)),
    ("rgb_mab_para.icc", (1.0, 0.0, 0.0)),
    ("rgb_mab_para.icc", (0.5, 0.0, 0.0)),
]


def parse_xyz(line: str) -> tuple[float, float, float]:
    nums = re.findall(r"[-+]?(?:\d+\.\d+|\d*\.\d+)(?:[eE][-+]?\d+)?", line)
    if len(nums) < 3:
        raise ValueError("no XYZ in %r" % line)
    return float(nums[0]), float(nums[1]), float(nums[2])


def our_xyz(lookup: str, profile: str, device: tuple[float, ...]):
    cmd = [lookup, os.path.join(ICC, profile)] + [str(v) for v in device]
    finished = subprocess.run(cmd, capture_output=True, text=True)
    if finished.returncode != 0:
        raise RuntimeError(
            "gcol_icc_lookup failed for %s %s:\n%s\n%s"
            % (profile, device, finished.stdout, finished.stderr))
    line = finished.stdout.strip().splitlines()[-1]
    return parse_xyz(line)


def lcms_xyz(profile: str, device: tuple[float, ...]):
    path = os.path.join(ICC, profile)
    argv = oracle_env.command(
        "lcms2",
        ["lcms_icc_lookup", path] + ["%.10f" % v for v in device])
    finished = subprocess.run(argv, capture_output=True, text=True)
    if finished.returncode != 0:
        raise RuntimeError(
            "lcms_icc_lookup failed for %s %s:\n%s\n%s"
            % (profile, device, finished.stdout, finished.stderr))
    lines = [ln for ln in finished.stdout.splitlines() if ln.strip()]
    if not lines:
        raise RuntimeError(
            "lcms_icc_lookup produced no output for %s: stderr=%r"
            % (profile, finished.stderr))
    return parse_xyz(lines[-1])


def main(argv) -> int:
    if len(argv) != 2:
        sys.stderr.write(
            "usage: verify_lcms.py <path-to-gcol_icc_lookup>\n")
        return 2
    lookup = argv[1]
    if not os.path.isfile(lookup):
        sys.stderr.write("verify_lcms: missing lookup tool: %s\n" % lookup)
        return 1

    print(oracle_env.provenance(["lcms2"]), flush=True)

    worst = 0.0
    failures = 0
    for profile, device in CASES:
        ours = our_xyz(lookup, profile, device)
        theirs = lcms_xyz(profile, device)
        delta = max(abs(ours[i] - theirs[i]) for i in range(3))
        worst = max(worst, delta)
        status = "ok" if delta <= TOL else "FAIL"
        print(
            "%s %s  ours=(%.6f %.6f %.6f)  lcms=(%.6f %.6f %.6f)  "
            "Δ=%.2e  %s"
            % (profile, device, ours[0], ours[1], ours[2],
               theirs[0], theirs[1], theirs[2], delta, status))
        if delta > TOL:
            failures += 1

    if failures:
        sys.stderr.write(
            "verify_lcms: %d case(s) exceeded %.1e (worst Δ=%.2e)\n"
            % (failures, TOL, worst))
        return 1
    print("verify_lcms: %d cases, worst Δ=%.2e" % (len(CASES), worst))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except oracle_env.OracleUnavailable as why:
        sys.stderr.write("verify_lcms: %s\n" % why)
        sys.exit(1)
