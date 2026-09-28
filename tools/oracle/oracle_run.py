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
"""Run one oracle gate, having first proved its reference is reachable.

    oracle_run.py <name>[,<name>...] -- <command> [args...]

With GHOTI_ORACLE_REQUIRED=1 an unreachable reference is an error. Without
it the process declines, prints SKIPPED, and exits 0.
"""

import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import oracle_env


def main(argv):
    if "--" not in argv:
        sys.stderr.write("usage: oracle_run.py <name>[,<name>] -- <command>\n")
        return 2
    cut = argv.index("--")
    names = [n for n in argv[1:cut][0].split(",") if n]
    command = argv[cut + 1:]
    required = os.environ.get("GHOTI_ORACLE_REQUIRED", "0") == "1"

    try:
        line = oracle_env.provenance(names)
    except oracle_env.OracleUnavailable as why:
        where = " ".join(command[:3])
        if required:
            sys.stderr.write(
                "### %s: the reference is not reachable ###\n%s\n"
                % (where, why))
            return 1
        sys.stderr.write("SKIPPED %s\n  %s\n" % (where, why))
        return 0
    print(line, flush=True)
    return subprocess.run(command).returncode


if __name__ == "__main__":
    sys.exit(main(sys.argv))
