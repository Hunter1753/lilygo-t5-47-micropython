#!/usr/bin/env python3
"""
convert_fonts.py — Batch-convert all fonts in the fonts/ folder.

Usage:
    python convert_fonts.py <size1> [<size2> ...] [--compress]

Arguments:
    size(s)     One or more point sizes to generate for every font found.

Options:
    --compress  Compress glyph bitmaps with zlib (recommended, saves flash).

Scans fonts/*.ttf and fonts/*.otf. The font family name is derived from the
filename stem using CamelCase conversion:
    roboto-regular.ttf  →  RobotoRegular
    FiraSans.otf        →  FiraSans
    my_font.ttf         →  MyFont

Generated headers land in module/userfonts/<FontName>/
The registry module/userfonts/userfonts.h is regenerated at the end.

Example:
    python convert_fonts.py 16 24 32 --compress
"""

import argparse
import os
import re
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
FONTS_DIR = os.path.join(SCRIPT_DIR, "fonts")

# Import helpers from add_font so all logic lives in one place.
sys.path.insert(0, SCRIPT_DIR)
from add_font import convert_font, regenerate_registry  # noqa: E402


def _stem_to_camel(stem: str) -> str:
    """
    Convert a filename stem to a CamelCase C identifier.

    Splits on non-alphanumeric characters and capitalises each word.
    Examples:
        roboto-regular  →  RobotoRegular
        FiraSans        →  FiraSans
        my_font_v2      →  MyFontV2
    """
    words = re.split(r"[^A-Za-z0-9]+", stem)
    return "".join(w.capitalize() for w in words if w)


def main():
    parser = argparse.ArgumentParser(
        description="Batch-convert all fonts in fonts/ to epdiy C headers.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    parser.add_argument("sizes", nargs="+", type=int, help="Point sizes to generate.")
    parser.add_argument("--compress", action="store_true", help="Compress glyph bitmaps.")
    args = parser.parse_args()

    if not os.path.isdir(FONTS_DIR):
        sys.exit(
            f"Error: fonts/ directory not found at {FONTS_DIR}\n"
            "Create it and place your .ttf / .otf files inside."
        )

    font_files = sorted(
        f for f in os.listdir(FONTS_DIR)
        if f.lower().endswith((".ttf", ".otf"))
    )

    if not font_files:
        sys.exit("No .ttf or .otf files found in fonts/")

    print(f"Found {len(font_files)} font file(s), generating sizes: {args.sizes}\n")

    converted = 0
    errors = 0
    for filename in font_files:
        stem = os.path.splitext(filename)[0]
        font_name = _stem_to_camel(stem)
        font_path = os.path.join(FONTS_DIR, filename)
        for size in args.sizes:
            try:
                convert_font(font_path, font_name, size, args.compress)
                converted += 1
            except SystemExit as exc:
                print(f"  Warning: {exc}", file=sys.stderr)
                errors += 1

    print()
    regenerate_registry()

    if errors:
        print(f"\n{converted} font(s) converted, {errors} error(s) — see warnings above.",
              file=sys.stderr)
        sys.exit(1)
    else:
        print(f"\n{converted} font(s) converted successfully.")


if __name__ == "__main__":
    main()
