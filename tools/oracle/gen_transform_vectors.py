#!/usr/bin/env python3
# SPDX-License-Identifier: LGPL-3.0-only
#
# Copyright (C) 2026 Corey Pennycuff
#
# This file is part of Ghoti.io Color.
#
# Ghoti.io Color is free software: you can redistribute it and/or modify it
# under the terms of the GNU Lesser General Public License version 3 as
# published by the Free Software Foundation.
#
# Ghoti.io Color is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
# License for more details.
#
# You should have received a copy of the GNU Lesser General Public License
# along with this program.  If not, see <https://www.gnu.org/licenses/>.

"""Regenerate tests/data/transform/lcms.txt from the pinned littleCMS.

    gen_transform_vectors.py

Runs lcms_transform_vectors (built into the `lcms2` image) against
tests/data/icc/gray_g22.icc and writes what it answered, with the version
that answered it. make test reads the committed file; nothing here runs on
a plain build.
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import oracle_env  # noqa: E402

OUT = os.path.join(ROOT, "tests", "data", "transform", "lcms.txt")


def main():
    try:
        version = oracle_env.check_pin("lcms2")
    except oracle_env.OracleUnavailable as why:
        sys.stderr.write("gen_transform_vectors: %s\n" % why)
        return 1
    gray = os.path.join(ROOT, "tests", "data", "icc", "gray_g22.icc")
    argv = oracle_env.command("lcms2", ["lcms_transform_vectors", gray])
    done = subprocess.run(argv, capture_output=True, text=True)
    if done.returncode != 0 or "# gray_to_srgb_f" not in done.stdout:
        sys.stderr.write("gen_transform_vectors: the probe failed\n%s"
                         % done.stderr)
        return 1
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="ascii") as handle:
        handle.write("## Answered by %s (lcms_transform_vectors, relative "
                     "colorimetric),\n## written by "
                     "tools/oracle/gen_transform_vectors.py.\n" % version)
        handle.write(done.stdout)
    print("wrote %s (%s)" % (os.path.relpath(OUT, ROOT), version))
    return 0


if __name__ == "__main__":
    sys.exit(main())
