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
"""Compare this library's device→PCS XYZ against ArgyllCMS icclu.

Independence: ArgyllCMS is a second CMM, not littleCMS and not generate.py.
ICC v4 (rgb_mab.icc) is outside this pin — Debian's Argyll 3.3 refuses v4.

    python3 tools/oracle/verify_argyll.py \\
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

# Absolute XYZ agreement. Matrix path should be tighter; LUT interpolation of
# a 2-point grid is coarser, so the shared bound is the LUT one.
TOL = 1e-3

# (profile, device channels, notes). rgb_mab is v4 and is deliberately absent.
CASES = [
    ("swap_rg.icc", (1.0, 0.0, 0.0)),
    ("swap_rg.icc", (0.0, 1.0, 0.0)),
    ("swap_rg.icc", (1.0, 1.0, 1.0)),
    ("srgb_g22.icc", (1.0, 0.0, 0.0)),
    ("srgb_g22.icc", (1.0, 1.0, 1.0)),
    ("rgb_mft1.icc", (1.0, 0.0, 0.0)),
    ("rgb_mft1.icc", (0.0, 1.0, 0.0)),
    ("rgb_mft1.icc", (1.0, 1.0, 1.0)),
    ("cmyk_mft2.icc", (0.0, 0.0, 0.0, 0.0)),
    ("cmyk_mft2.icc", (1.0, 0.0, 0.0, 0.0)),
    ("cmyk_mft2.icc", (0.0, 0.0, 0.0, 1.0)),
]


def parse_xyz(line: str) -> tuple[float, float, float]:
    # Prefer the right-hand side of the last "->"; fall back to the whole line.
    right = line.rsplit("->", 1)[-1]
    # Require a decimal point so tag names like "A2B0" are not numbers.
    nums = re.findall(r"[-+]?(?:\d+\.\d+|\d*\.\d+)(?:[eE][-+]?\d+)?", right)
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


def argyll_xyz(profile: str, device: tuple[float, ...]):
    path = os.path.join(ICC, profile)
    argv = oracle_env.command(
        "argyll", ["icclu", "-s", "1", "-ir", "-ff", path])
    payload = " ".join("%.10f" % v for v in device) + "\n"
    finished = subprocess.run(
        argv, input=payload, capture_output=True, text=True)
    if finished.returncode != 0:
        raise RuntimeError(
            "icclu failed for %s %s:\n%s\n%s"
            % (profile, device, finished.stdout, finished.stderr))
    # icclu writes the answer on stdout; warnings may land on stderr.
    lines = [ln for ln in finished.stdout.splitlines() if "->" in ln]
    if not lines:
        raise RuntimeError(
            "icclu produced no lookup line for %s: %r"
            % (profile, finished.stdout))
    return parse_xyz(lines[-1])


def main(argv) -> int:
    if len(argv) != 2:
        sys.stderr.write(
            "usage: verify_argyll.py <path-to-gcol_icc_lookup>\n")
        return 2
    lookup = argv[1]
    if not os.path.isfile(lookup):
        sys.stderr.write("verify_argyll: missing lookup tool: %s\n" % lookup)
        return 1

    print(oracle_env.provenance(["argyll"]), flush=True)

    worst = 0.0
    failures = 0
    for profile, device in CASES:
        ours = our_xyz(lookup, profile, device)
        theirs = argyll_xyz(profile, device)
        delta = max(abs(ours[i] - theirs[i]) for i in range(3))
        worst = max(worst, delta)
        status = "ok" if delta <= TOL else "FAIL"
        print(
            "%s %s  ours=(%.6f %.6f %.6f)  argyll=(%.6f %.6f %.6f)  "
            "Δ=%.2e  %s"
            % (profile, device, ours[0], ours[1], ours[2],
               theirs[0], theirs[1], theirs[2], delta, status))
        if delta > TOL:
            failures += 1

    if failures:
        sys.stderr.write(
            "verify_argyll: %d case(s) exceeded %.1e (worst Δ=%.2e)\n"
            % (failures, TOL, worst))
        return 1
    print("verify_argyll: %d cases, worst Δ=%.2e" % (len(CASES), worst))
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except oracle_env.OracleUnavailable as why:
        sys.stderr.write("verify_argyll: %s\n" % why)
        sys.exit(1)
