#!/usr/bin/env python3
"""Regenerate every embedded Terminal font from the complete IBM Plex Mono cmap."""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
from pathlib import Path

from fontTools.ttLib import TTFont


FONT_SIZES = range(8, 26, 2)
REPOSITORY = Path(__file__).resolve().parents[1]
DEFAULT_FIRMWARE = REPOSITORY.parent / "crosspoint-reader"


def contiguous_ranges(codepoints: list[int]) -> list[tuple[int, int]]:
    if not codepoints:
        raise SystemExit("IBM Plex Mono has no Unicode cmap")
    ranges: list[tuple[int, int]] = []
    start = previous = codepoints[0]
    for codepoint in codepoints[1:]:
        if codepoint == previous + 1:
            previous = codepoint
            continue
        ranges.append((start, previous))
        start = previous = codepoint
    ranges.append((start, previous))
    return ranges


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--firmware",
        type=Path,
        default=DEFAULT_FIRMWARE,
        help="CrossPoint firmware checkout containing EpdFont tools and IBM Plex Mono",
    )
    parser.add_argument(
        "--python",
        default=sys.executable,
        help="Python interpreter with the EpdFont generator dependencies",
    )
    args = parser.parse_args()

    firmware = args.firmware.resolve()
    converter = firmware / "lib" / "EpdFont" / "scripts" / "fontconvert.py"
    source_font = (
        firmware
        / "lib"
        / "EpdFont"
        / "builtinFonts"
        / "source"
        / "IBMPlexMono"
        / "IBMPlexMono-Regular.ttf"
    )
    source_argument = "../builtinFonts/source/IBMPlexMono/IBMPlexMono-Regular.ttf"
    for required in (converter, source_font):
        if not required.is_file():
            raise SystemExit(f"required font source was not found: {required}")

    with TTFont(source_font, lazy=True) as font:
        cmap = font.getBestCmap() or {}
        codepoints = sorted(cmap)
    ranges = contiguous_ranges(codepoints)
    interval_arguments = [
        argument
        for start, end in ranges
        for argument in ("--additional-intervals", f"0x{start:X},0x{end:X}")
    ]

    destination = REPOSITORY / "include" / "builtinFonts"
    destination.mkdir(parents=True, exist_ok=True)
    for size in FONT_SIZES:
        name = f"terminalmono_{size}_regular"
        output = destination / f"{name}.h"
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=destination, delete=False
        ) as temporary:
            temporary_path = Path(temporary.name)
            try:
                subprocess.run(
                    [
                        args.python,
                        "fontconvert.py",
                        name,
                        str(size),
                        source_argument,
                        "--2bit",
                        "--compress",
                        "--zopfli",
                        *interval_arguments,
                    ],
                    check=True,
                    cwd=converter.parent,
                    stdout=temporary,
                )
            except BaseException:
                temporary_path.unlink(missing_ok=True)
                raise
        temporary_path.replace(output)
        print(f"generated {output.relative_to(REPOSITORY)} ({len(codepoints)} cmap glyphs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
