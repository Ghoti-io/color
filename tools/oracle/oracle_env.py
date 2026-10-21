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
"""How an oracle is spelled, so that no tool here spells one itself.

Copied from the archive library's oracle_env.py, which is the built-here
shape: the image has no upstream digest, so check_pin() runs in both modes
and the version the reference prints is the guarantee. A probe that exits
non-zero is a failure even if its stdout happens to contain the expected
text.

`command("argyll")` returns the argv prefix that runs inside the image built
by `containers/argyll/Containerfile`. Lookups use `icclu` (bundled icclib);
the version pin is taken from `xicclu`'s banner, which names the ArgyllCMS
release. `command("lcms2")` is the littleCMS 2 image
(`containers/lcms2/Containerfile`); lookups use `lcms_icc_lookup`.
`command("libtiff")` is libtiff 4.7.0 (`containers/libtiff/Containerfile`),
asked about SGI LogLuv through `logluv_vectors`.

Modes, from GHOTI_ORACLE_MODE:

  container  (default) run the reference in its pinned image
  host                 run a binary of the same name on this machine, and
                       still require it to report the pinned version
"""

import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
IMAGES = os.path.join(HERE, "containers", "IMAGES")
ARGYLL = os.path.join(HERE, "containers", "argyll")
LCMS2 = os.path.join(HERE, "containers", "lcms2")
LIBTIFF = os.path.join(HERE, "containers", "libtiff")

MODE = os.environ.get("GHOTI_ORACLE_MODE", "container")
ENGINE = os.environ.get("GHOTI_CONTAINER_ENGINE", "docker")


class OracleUnavailable(Exception):
    """The reference cannot be reached. Never caught into a skip by a gate
    that was asked to judge."""


# The probe is a program in the image, not a shell one-liner here.
PROBE = {
    "argyll": (["argyll-version"], "Lookup ICC or CAL colors, Version 3.3.0"),
    "lcms2": (["lcms2-version"], "LittleCMS 2.16"),
    "libtiff": (["libtiff-version"], "libtiff 4.7.0"),
}

# Host mode checks this binary's presence; the probe script is how its
# version is read.
BINARY = {
    "argyll": "xicclu",
    "lcms2": "lcms_icc_lookup",
    "libtiff": "logluv_vectors",
}

_pins = None
_cache = {}


def pins():
    """The IMAGES table: name -> (image, version, description)."""
    global _pins
    if _pins is not None:
        return _pins
    _pins = {}
    if not os.path.exists(IMAGES):
        return _pins
    with open(IMAGES, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                raise OracleUnavailable(
                    "containers/IMAGES: not three tab-separated fields: %r"
                    % line)
            name, image, version = parts[0], parts[1], parts[2]
            _pins[name] = (image, version, parts[3] if len(parts) > 3 else "")
    return _pins


def _engine_ok():
    if shutil.which(ENGINE) is None:
        raise OracleUnavailable(
            "%s is not on PATH, and GHOTI_ORACLE_MODE is 'container'.\n"
            "Install it, or name a different engine with "
            "GHOTI_CONTAINER_ENGINE." % ENGINE)


def _have_image(image):
    finished = subprocess.run([ENGINE, "image", "exists", image],
        capture_output=True)
    if finished.returncode == 0:
        return True
    finished = subprocess.run([ENGINE, "image", "inspect", image],
        capture_output=True)
    return finished.returncode == 0


def ensure(name):
    """Make the reference runnable, or raise saying what is missing."""
    if MODE == "host":
        binary = BINARY.get(name, name)
        if shutil.which(binary) is None:
            raise OracleUnavailable(
                "GHOTI_ORACLE_MODE=host and %s is not on PATH" % binary)
        return
    if MODE != "container":
        raise OracleUnavailable("GHOTI_ORACLE_MODE=%r is not a mode" % MODE)
    _engine_ok()
    table = pins()
    if name not in table:
        raise OracleUnavailable(
            "no pin for %r in tools/oracle/containers/IMAGES" % name)
    image = table[name][0]
    if _have_image(image):
        return
    if os.environ.get("GHOTI_ORACLE_PULL", "1") != "1":
        raise OracleUnavailable(
            "image for %s is not present and GHOTI_ORACLE_PULL is off: %s"
            % (name, image))
    if image.startswith("localhost/"):
        raise OracleUnavailable(
            "image for %s is not present. It is built here, not pulled:\n"
            "  make oracle-build\n  %s" % (name, image))
    sys.stderr.write("oracle: pulling %s\n" % image)
    finished = subprocess.run([ENGINE, "pull", image], capture_output=True,
        text=True)
    if finished.returncode != 0:
        raise OracleUnavailable(
            "could not pull the pinned image for %s.\n  %s\n%s"
            % (name, image, finished.stderr.strip()))


ALIAS = dict(
    pair.split("=", 1)
    for pair in os.environ.get("GHOTI_ORACLE_ALIAS", "").split(",")
    if "=" in pair)


def _argv(name, argv):
    """Where a probe script is found.

    In the image it is on PATH. On the host it is the same file in this tree.
    """
    if MODE != "host":
        return list(argv)
    host_dirs = {
        "argyll": ARGYLL,
        "lcms2": LCMS2,
        "libtiff": LIBTIFF,
    }
    base = host_dirs.get(name, HERE)
    rewritten = []
    for word in argv:
        local = os.path.join(base, word)
        rewritten.append(local if os.path.isfile(local) else word)
    return rewritten


_rootless = None


def _engine_maps_user():
    """Whether the engine runs rootless, so container root is the caller.

    A rootless engine (podman by default, docker in its rootless mode) maps
    root inside the container to whoever started it, and what a reference
    writes into a mounted directory is theirs. A rootful one (docker's usual
    install, or podman run as root) does not: the files are owned by root,
    and the caller cannot replace or delete them afterwards - found on a
    rootful docker install, 2026-10-17, where a second `make oracle-tools` failed on
    every file the first had written. Passing --user fixes the rootful case
    and breaks the rootless one, where it would make the files a subordinate
    uid's, so the engine is asked rather than guessed from its name.
    """
    global _rootless
    if _rootless is None:
        def ask(*args):
            try:
                return subprocess.run([ENGINE] + list(args),
                    capture_output=True, text=True,
                    stdin=subprocess.DEVNULL).stdout
            except OSError:
                return ""
        if "podman" in ask("--version").lower():
            _rootless = ask("info", "--format",
                "{{.Host.Security.Rootless}}").strip() == "true"
        else:
            _rootless = "rootless" in ask("info", "--format",
                "{{json .SecurityOptions}}")
    return _rootless


def command(name, argv=None, scratch=None):
    """The argv prefix that runs `name`'s reference.

    The repository is bind-mounted at its own path, read-only. `--network none`
    because a reference that can fetch something mid-run is not the pin.
    `scratch`, if given, is mounted read-write under the same path.
    """
    name = ALIAS.get(name, name)
    ensure(name)
    inner = argv if argv is not None else [PROBE.get(name, ([name],))[0][0]]
    inner = _argv(name, inner)
    if MODE == "host":
        return list(inner)
    image = pins()[name][0]
    argv_out = [ENGINE, "run", "--rm", "-i",
            "--network", "none",
            "--volume", "%s:%s:ro" % (ROOT, ROOT)]
    if not _engine_maps_user() and hasattr(os, "getuid"):
        # Run as the caller, so what the reference writes is the caller's to
        # replace. HOME is somewhere it can write, which / is not.
        argv_out += ["--user", "%d:%d" % (os.getuid(), os.getgid()),
                     "--env", "HOME=/tmp"]
    for path in ([scratch] if isinstance(scratch, str) else (scratch or [])):
        argv_out += ["--volume", "%s:%s:rw" % (path, path)]
    return argv_out + ["--workdir", ROOT, image] + list(inner)


def version(name):
    """What the reference says it is. Runs it; the answer is cached."""
    name = ALIAS.get(name, name)
    key = ("version", name)
    if key in _cache:
        return _cache[key]
    probe, expect = PROBE.get(name, ([name, "--version"], ""))
    finished = subprocess.run(command(name, probe), capture_output=True,
        text=True)
    text = finished.stdout.strip().splitlines()
    text = text[0] if text else ""
    if finished.returncode != 0:
        raise OracleUnavailable(
            "%s probe exited %s: %r" % (name, finished.returncode, text))
    if expect and expect not in text:
        raise OracleUnavailable(
            "%s answered %r, which does not contain %r" % (name, text, expect))
    _cache[key] = text
    return text


def check_pin(name):
    """Raise unless the reference's version matches containers/IMAGES."""
    name = ALIAS.get(name, name)
    table = pins()
    if name not in table:
        return version(name)
    said = table[name][1]
    got = version(name)
    if said not in got:
        raise OracleUnavailable(
            "%s: IMAGES says %s and it answers %r" % (name, said, got))
    return got


def provenance(names):
    """One line naming every reference that answered, and how."""
    where = MODE
    parts = []
    for name in names:
        resolved = ALIAS.get(name, name)
        label = resolved if resolved == name else "%s as %s" % (resolved, name)
        parts.append("%s %s" % (label, check_pin(name)))
    return "oracle(%s): %s" % (where, ", ".join(parts))
