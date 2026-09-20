#!/usr/bin/env python3
"""Render the application icon from misc/common/icons/qspgui.glyph.

The glyph set is the source of truth: a 16-cell grid plus one scene per
shipped size. This script runs asciipng once per size -- so every size is
drawn at its own scale rather than downsampled off a 1024 master, which is
the whole point of pixel art -- and then packs the results into the four
shapes the build actually consumes:

    misc/common/icons/logo.ico    the Windows resource, via qspgui/rsc/res.rc
    qspgui/icons/logo.xpm         the wxICON() fallback on GTK and macOS
    qspgui/icons/logo_big.xpm     the About box
    misc/macos/icon.icns          the bundle icon
    misc/common/icons/qsp.svg     installed into hicolor/scalable on Linux

asciipng is not vendored. Point at it with --asciipng or $ASCIIPNG; without
either the script looks for `asciipng` on PATH.

    python tools/make_icons.py --asciipng /path/to/asciipng.exe

Needs Pillow only to read back the PNGs asciipng writes; the .ico, .xpm,
.icns and .svg writers below are all plain struct and text.
"""

from __future__ import annotations

import argparse
import io
import os
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from PIL import Image

REPO = Path(__file__).resolve().parent.parent
GLYPH = REPO / "misc" / "common" / "icons" / "qspgui.glyph"

# size -> (scene, scale). Anything above 64 re-runs the 64 scene bigger, which
# is an exact pixel doubling; everything below it is drawn on its own,
# because its chrome is not a scaled copy of the 64's.
RENDERS = {
    16: ("mark16", 1),
    20: ("tile20", 1),
    24: ("tile24", 1),
    32: ("tile32", 1),
    48: ("tile48", 1),
    64: ("tile64", 1),
    128: ("tile64", 2),
    256: ("tile64", 4),
    512: ("tile64", 8),
    1024: ("tile64", 16),
}

# 20 and 24 are what Windows asks for at 125% and 150% display scaling.
ICO_SIZES = [16, 20, 24, 32, 48, 64, 128, 256]

# The .icns chunk types, and which rendered size each one takes. The @2x
# types are the same pixels as the plain type at twice the size -- that is
# what a retina variant is.
ICNS_TYPES = [
    ("icp4", 16),
    ("icp5", 32),
    ("ic11", 32),    # 16x16@2x
    ("ic12", 64),    # 32x32@2x
    ("ic07", 128),
    ("ic13", 256),   # 128x128@2x
    ("ic08", 256),
    ("ic14", 512),   # 256x256@2x
    ("ic09", 512),
    ("ic10", 1024),  # 512x512@2x
]


def find_asciipng(explicit: str | None) -> str:
    for candidate in (explicit, os.environ.get("ASCIIPNG"), shutil.which("asciipng")):
        if candidate and Path(candidate).exists():
            return str(candidate)
        if candidate and shutil.which(candidate):
            return str(shutil.which(candidate))
    sys.exit(
        "asciipng not found. Pass --asciipng /path/to/asciipng.exe, set "
        "$ASCIIPNG, or put it on PATH."
    )


def render_all(asciipng: str, out_dir: Path) -> dict[int, Image.Image]:
    """One asciipng run per shipped size, read back as RGBA."""
    images: dict[int, Image.Image] = {}
    for size, (scene, scale) in sorted(RENDERS.items()):
        png = out_dir / f"{size}.png"
        subprocess.run(
            [asciipng, "scene", "qspgui", scene, "-o", str(png),
             "--scale", str(scale), "--strict"],
            cwd=GLYPH.parent, check=True,
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
        )
        im = Image.open(png).convert("RGBA")
        if im.size != (size, size):
            sys.exit(f"{scene} at x{scale} rendered {im.size}, wanted {size}x{size}")
        images[size] = im
    return images


def flatten_alpha(im: Image.Image) -> Image.Image:
    """Pixel art has no half-transparent pixels; make sure of it.

    The rounded corners come out of asciipng as hard on/off edges. If a pass
    ever starts feathering them the XPM writer would silently drop the
    feathered ring, so snap here and say so rather than find out later.
    """
    out = im.copy()
    px = out.load()
    fuzzy = 0
    for y in range(out.height):
        for x in range(out.width):
            r, g, b, a = px[x, y]
            if a not in (0, 255):
                fuzzy += 1
                px[x, y] = (r, g, b, 255 if a >= 128 else 0)
    if fuzzy:
        print(f"  note: snapped {fuzzy} partly-transparent pixels to on/off")
    return out


# --------------------------------------------------------------------- .ico

def bmp_entry(im: Image.Image) -> bytes:
    """A 32bpp BITMAPINFOHEADER image, bottom-up, with the AND mask ICO wants.

    The mask is redundant next to an alpha channel and Windows ignores it for
    32bpp icons, but the header's doubled height promises it is there and
    leaving it out truncates the entry.
    """
    w, h = im.size
    px = im.load()
    xor = bytearray()
    for y in range(h - 1, -1, -1):
        for x in range(w):
            r, g, b, a = px[x, y]
            xor += bytes((b, g, r, a))
    row_bytes = ((w + 31) // 32) * 4
    mask = bytearray()
    for y in range(h - 1, -1, -1):
        bits = bytearray(row_bytes)
        for x in range(w):
            if px[x, y][3] == 0:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bits
    header = struct.pack(
        "<IiiHHIIiiII",
        40, w, h * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0,
    )
    return bytes(header + xor + mask)


def write_ico(images: dict[int, Image.Image], path: Path) -> None:
    entries, blobs = [], []
    for size in ICO_SIZES:
        im = images[size]
        if size >= 256:
            buf = io.BytesIO()
            im.save(buf, "PNG", optimize=True)
            blobs.append(buf.getvalue())
        else:
            blobs.append(bmp_entry(im))
        entries.append(size)

    offset = 6 + 16 * len(entries)
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    for size, blob in zip(entries, blobs):
        dim = 0 if size >= 256 else size
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for blob in blobs:
        out += blob
    path.write_bytes(bytes(out))
    print(f"  {path.relative_to(REPO)}  {', '.join(str(s) for s in entries)}")


# --------------------------------------------------------------------- .xpm

# The characters an XPM may use for a pixel, minus the ones that would need
# escaping inside a C string.
XPM_CHARS = (
    "0123456789abcdefghijklmnopqrstuvwxyz"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "!#$%&'()*+,-./:;<=>?@[]^_`{|}~"
)


def write_xpm(im: Image.Image, path: Path, name: str) -> None:
    w, h = im.size
    px = im.load()
    colors: dict[tuple[int, int, int, int], str] = {}
    order: list[tuple[int, int, int, int]] = []
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            key = (0, 0, 0, 0) if a == 0 else (r, g, b, 255)
            if key not in colors:
                colors[key] = ""
                order.append(key)

    # One character per pixel while the palette fits, two once it does not.
    per = 1 if len(order) <= len(XPM_CHARS) else 2
    if per == 2 and len(order) > len(XPM_CHARS) ** 2:
        sys.exit(f"{name}: {len(order)} colours is more than two characters can index")
    for i, key in enumerate(order):
        if per == 1:
            colors[key] = XPM_CHARS[i]
        else:
            colors[key] = XPM_CHARS[i // len(XPM_CHARS)] + XPM_CHARS[i % len(XPM_CHARS)]

    lines = [
        "/* XPM */",
        f"static const char *{name}_xpm[] = {{",
        "/* columns rows colors chars-per-pixel */",
        f'"{w} {h} {len(order)} {per}",',
    ]
    for key in order:
        code = colors[key]
        spec = "None" if key[3] == 0 else "#%02X%02X%02X" % key[:3]
        lines.append(f'"{code} c {spec}",')
    lines.append("/* pixels */")
    for y in range(h):
        row = []
        for x in range(w):
            r, g, b, a = px[x, y]
            row.append(colors[(0, 0, 0, 0) if a == 0 else (r, g, b, 255)])
        comma = "," if y < h - 1 else ""
        lines.append(f'"{"".join(row)}"{comma}')
    lines.append("};")
    path.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"  {path.relative_to(REPO)}  {w}x{h}, {len(order)} colours, {per}/px")


# -------------------------------------------------------------------- .icns

def write_icns(images: dict[int, Image.Image], path: Path) -> None:
    chunks = bytearray()
    written = []
    for kind, size in ICNS_TYPES:
        buf = io.BytesIO()
        images[size].save(buf, "PNG", optimize=True)
        data = buf.getvalue()
        chunks += kind.encode("ascii") + struct.pack(">I", len(data) + 8) + data
        written.append(f"{kind}:{size}")
    out = b"icns" + struct.pack(">I", len(chunks) + 8) + bytes(chunks)
    path.write_bytes(out)
    print(f"  {path.relative_to(REPO)}  {' '.join(written)}")


# --------------------------------------------------------------------- .svg

def write_svg(im: Image.Image, path: Path) -> None:
    """The 64px tile as rectangles of flat colour, so it stays crisp at any size.

    Runs are found along each row and then merged downwards where the row
    below has the same run at the same place, which collapses the tile's flat
    interior from one rect per row into one rect.
    """
    w, h = im.size
    px = im.load()

    rows: list[list[tuple[int, int, tuple[int, int, int, int]]]] = []
    for y in range(h):
        runs = []
        x = 0
        while x < w:
            pixel = px[x, y]
            if pixel[3] == 0:
                x += 1
                continue
            run = 1
            while x + run < w and px[x + run, y] == pixel:
                run += 1
            runs.append((x, run, pixel))
            x += run
        rows.append(runs)

    rects = []
    taken = [set() for _ in range(h)]
    for y in range(h):
        for run in rows[y]:
            if run in taken[y]:
                continue
            height = 1
            while y + height < h and run in rows[y + height]:
                taken[y + height].add(run)
                height += 1
            x, width, (r, g, b, _) = run
            rects.append(
                f'<rect x="{x}" y="{y}" width="{width}" height="{height}" '
                f'fill="#{r:02x}{g:02x}{b:02x}"/>'
            )
    body = "\n    ".join(rects)
    path.write_text(
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w} {h}" '
        f'width="{w}" height="{h}" shape-rendering="crispEdges">\n'
        "    <title>Quest Soft Player</title>\n"
        f"    {body}\n"
        "</svg>\n",
        encoding="utf-8",
    )
    print(f"  {path.relative_to(REPO)}  {len(rects)} rects")


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--asciipng", help="path to the asciipng binary")
    ap.add_argument("--keep", metavar="DIR",
                    help="also leave the rendered PNGs here")
    args = ap.parse_args()

    asciipng = find_asciipng(args.asciipng)
    with tempfile.TemporaryDirectory() as tmp:
        print(f"rendering {GLYPH.relative_to(REPO)} with {asciipng}")
        images = {s: flatten_alpha(im)
                  for s, im in render_all(asciipng, Path(tmp)).items()}

        write_ico(images, REPO / "misc" / "common" / "icons" / "logo.ico")
        write_xpm(images[32], REPO / "qspgui" / "icons" / "logo.xpm", "logo")
        write_xpm(images[64], REPO / "qspgui" / "icons" / "logo_big.xpm", "logo_big")
        write_icns(images, REPO / "misc" / "macos" / "icon.icns")
        write_svg(images[64], REPO / "misc" / "common" / "icons" / "qsp.svg")

        if args.keep:
            dest = Path(args.keep)
            dest.mkdir(parents=True, exist_ok=True)
            for size, im in images.items():
                im.save(dest / f"qspgui-{size}.png")
            print(f"  PNGs in {dest}")


if __name__ == "__main__":
    main()
