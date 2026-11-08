#!/usr/bin/env python3
"""Generates the Palm OS bitmaps for Jakesoft Checklists.

  icon_{1,2,4,8}bpp.bmp   32x22 launcher icon
  small_{1,2,4,8}bpp.bmp  15x9 small launcher icon
  logo_{1,2,4,8}bpp.bmp   120x45 Jakesoft logo (from jakesoft-logo.svg)
  icon.svg                128x128 icon in the Rabbit r1 creation style
  preview_*.png           enlarged previews

Palm conventions: palette index 0 is white, the highest gray index is black.
The 2-bit gray images are stored in 4-bit BMP files (BMP has no 2-bit format)
using gray levels 0, 5, 10, 15 of a 16-step gray palette.

Run from anywhere:  python make_assets.py   (in the Tools folder)
Reads jakesoft-logo.svg from Tools, writes .bmp files to ../Rsc, and writes
icon.svg, logo_big.png and preview_*.png to Tools.
"""
import os
import struct
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))       # Tools folder
RSC = os.path.join(HERE, "..", "Rsc")                    # .bmp files go here

ORANGE = (0xFE, 0x50, 0x00)      # Rabbit r1 orange
WHITE = (255, 255, 255)
NAVY = (0x0A, 0x2A, 0x6B)
BLUE = (0x3E, 0x7B, 0xFA)


# ---------------------------------------------------------------- BMP writer
def write_bmp(path, w, h, bpp, palette, rows):
    """rows: list of h lists of palette indices, top row first."""
    row_bytes = ((w * bpp + 31) // 32) * 4
    ncolors = 1 << bpp
    pal = list(palette) + [(0, 0, 0)] * (ncolors - len(palette))
    pal_bytes = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in pal)
    data = bytearray()
    for row in reversed(rows):
        line = bytearray(row_bytes)
        for x, idx in enumerate(row):
            bit = x * bpp
            if bpp == 8:
                line[x] = idx
            else:
                shift = 8 - bpp - (bit % 8)
                line[bit // 8] |= idx << shift
        data += line
    off = 14 + 40 + len(pal_bytes)
    hdr = b"BM" + struct.pack("<IHHI", off + len(data), 0, 0, off)
    info = struct.pack("<IiiHHIIiiII", 40, w, h, 1, bpp, 0, len(data),
                       2835, 2835, ncolors, 0)
    with open(os.path.join(RSC, os.path.basename(path)), "wb") as f:
        f.write(hdr + info + pal_bytes + bytes(data))


GRAY16 = [(255 - i * 17,) * 3 for i in range(16)]       # 0 white .. 15 black


def write_set(prefix, w, h, dark):
    """dark: rows of floats 0..1 (0 white, 1 black) for gray/mono output,
    plus a color image handled separately by the caller."""
    # 1-bit
    write_bmp(f"{prefix}_1bpp.bmp", w, h, 1, [(255, 255, 255), (0, 0, 0)],
              [[1 if v >= 0.5 else 0 for v in r] for r in dark])
    # 2-bit gray, stored in a 4-bit BMP at levels 0,5,10,15
    write_bmp(f"{prefix}_2bpp.bmp", w, h, 4, GRAY16,
              [[round(v * 3) * 5 for v in r] for r in dark])
    # 4-bit gray
    write_bmp(f"{prefix}_4bpp.bmp", w, h, 4, GRAY16,
              [[round(v * 15) for v in r] for r in dark])


def write_color(prefix, img):
    q = img.convert("RGB").quantize(colors=16, method=Image.Quantize.MEDIANCUT,
                                    dither=Image.Dither.NONE)
    pal = (q.getpalette() + [0] * 48)[:48]
    palette = [tuple(pal[i:i + 3]) for i in range(0, 48, 3)]
    # keep white as index 0 like the Palm palette
    wi = min(range(16), key=lambda i: sum((255 - c) for c in palette[i]))
    order = [wi] + [i for i in range(16) if i != wi]
    remap = {old: new for new, old in enumerate(order)}
    palette = [palette[i] for i in order]
    px = q.load()
    w, h = q.size
    rows = [[remap[px[x, y]] for x in range(w)] for y in range(h)]
    write_bmp(f"{prefix}_8bpp.bmp", w, h, 8, palette, rows)


# ---------------------------------------------------------------- icons
def icon_pixels(w, h, small):
    """Pixel art grid: '.' transparent/white, '#' orange, 'o' white glyph.
    A rounded orange tile holding a big white checkbox with an orange tick."""
    g = [["." for _ in range(w)] for _ in range(h)]
    for y in range(h):
        for x in range(w):
            g[y][x] = "#"
    # rounded tile corners
    r = 1 if small else 2
    for k in range(r):
        for (cx, cy) in [(k, 0), (0, k)]:
            g[cy][cx] = "."
            g[cy][w - 1 - cx] = "."
            g[h - 1 - cy][cx] = "."
            g[h - 1 - cy][w - 1 - cx] = "."
    def seg(p, q):
        (x0, y0), (x1, y1) = p, q
        n = max(abs(x1 - x0), abs(y1 - y0))
        return [(x0 + round((x1 - x0) * i / n), y0 + round((y1 - y0) * i / n))
                for i in range(n + 1)]

    if small:                                   # 15x9: 7x7 box
        bx, by, bs = 4, 1, 7
        pts = seg((1, 3), (2, 4)) + seg((2, 4), (5, 1))
        thick = 1
    else:                                       # 32x22: 16x16 box
        bx, by, bs = 8, 3, 16
        pts = seg((3, 8), (6, 11)) + seg((6, 11), (13, 4))
        thick = 2
    for y in range(bs):
        for x in range(bs):
            g[by + y][bx + x] = "o"
    for (tx, ty) in pts:
        for t in range(thick + 1):              # extra rows below = bolder stroke
            if ty + t < bs - 1:
                g[by + ty + t][bx + tx] = "#"
    return g


def make_icon(prefix, w, h, small):
    g = icon_pixels(w, h, small)
    color = Image.new("RGB", (w, h), WHITE)
    cp = color.load()
    dark_gray = [[0.0] * w for _ in range(h)]
    dark_mono = [[0.0] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            if g[y][x] == "#":
                cp[x, y] = ORANGE
                dark_gray[y][x] = 0.67
                dark_mono[y][x] = 1.0
    write_bmp(f"{prefix}_1bpp.bmp", w, h, 1, [(255, 255, 255), (0, 0, 0)],
              [[1 if v >= 0.5 else 0 for v in r] for r in dark_mono])
    write_bmp(f"{prefix}_2bpp.bmp", w, h, 4, GRAY16,
              [[round(v * 3) * 5 for v in r] for r in dark_gray])
    write_bmp(f"{prefix}_4bpp.bmp", w, h, 4, GRAY16,
              [[round(v * 15) for v in r] for r in dark_gray])
    write_color(prefix, color)
    color.resize((w * 8, h * 8), Image.NEAREST).save(
        os.path.join(HERE, f"preview_{prefix}.png"))


# ---------------------------------------------------------------- SVG icon
def make_svg():
    svg = """<svg xmlns="http://www.w3.org/2000/svg" width="128" height="128" viewBox="0 0 128 128">
  <rect width="128" height="128" rx="28" fill="#FE5000"/>
  <rect x="28" y="28" width="72" height="72" rx="14" fill="#fff"/>
  <path d="M46 66l13 13 24-28" fill="none" stroke="#FE5000" stroke-width="11"
        stroke-linecap="round" stroke-linejoin="round"/>
</svg>
"""
    open(os.path.join(HERE, "icon.svg"), "w").write(svg)


# ---------------------------------------------------------------- logo
def make_logo():
    """Renders jakesoft-logo.svg (640x240) down to 120x45 for the About box."""
    import subprocess
    W, H, S = 120, 45, 8
    big_png = os.path.join(HERE, "logo_big.png")
    import shutil
    if shutil.which("rsvg-convert"):
        subprocess.run(["rsvg-convert", "-w", str(W * S), "-h", str(H * S),
                        "-o", big_png, os.path.join(HERE, "jakesoft-logo.svg")],
                       check=True)
    elif os.path.exists(big_png):
        print("rsvg-convert not found; using existing logo_big.png")
    else:
        raise SystemExit("Need rsvg-convert, or a %dx%d logo_big.png in Tools"
                         % (W * S, H * S))
    big = Image.open(big_png).convert("RGB")
    color = big.resize((W, H), Image.LANCZOS)
    lum = color.convert("L")
    dark = [[1 - lum.getpixel((x, y)) / 255 for x in range(W)] for y in range(H)]
    write_set("logo", W, H, dark)
    write_color("logo", color)
    color.resize((W * 4, H * 4), Image.NEAREST).save(
        os.path.join(HERE, "preview_logo.png"))


if __name__ == "__main__":
    make_icon("icon", 32, 22, small=False)
    make_icon("small", 15, 9, small=True)
    make_svg()
    make_logo()
    print("done")
