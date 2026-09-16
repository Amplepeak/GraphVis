#!/usr/bin/env python3
"""Build the passive audit's mark: the GraphVis otter, under a magnifier.

Why this is a script and not a one-off image
--------------------------------------------
The audit icon has been wrong three times, and each time the fix was a hand
edit nobody could reproduce. This takes the real logo as input and emits every
size the window and the taskbar need, so the next change is one edit here
rather than an image somebody has to find again.

What it emits, into assets/branding/:

    graphvis-audit.png         256x256, for Tk's iconphoto and anything else
    graphvis-audit-badge.png    48x48, for the window's own header - at the
                                EXACT display size, because Tk's PhotoImage
                                only downsamples by whole integers and a
                                badge scaled that way looks chewed
    graphvis-audit.ico         16/24/32/48/64/128/256, every entry a DIB

Two things about the ICO that cost a round each:

  * Tk on Windows cannot read a PNG-compressed ICO entry. PIL writes PNG
    entries for the larger sizes, so the entries are encoded here by hand as
    BITMAPINFOHEADER + BGRA + an AND mask, and then verified before the file
    is written. Verified, not assumed - that is the whole point.
  * A valid ICO is still not enough for the TASKBAR. See the note about
    AppUserModelID in tools/audit_window.py: a pythonw process inherits
    Python's taskbar identity, and the icon has nothing to do with it.

    python tools/make_audit_icon.py
"""
from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BRAND = ROOT / "assets" / "branding"
SOURCE = BRAND / "graphvis_icon.png"

ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
BADGE = 48
SS = 4                                   # supersample factor while drawing

INK = (11, 27, 43, 255)                  # the dark outline, GraphVis navy
RIM = (255, 255, 255, 255)


def build(source: Path):
    from PIL import Image, ImageDraw, ImageFilter

    logo = Image.open(source).convert("RGBA")
    n = 256 * SS
    canvas = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    canvas.paste(logo.resize((n, n), Image.LANCZOS), (0, 0))

    # --- the magnifier ----------------------------------------------------
    # Placed low and right, overlapping the logo's circle and running into the
    # corner the circle leaves empty. Big: at 16 pixels the otter is a teal
    # smudge whatever we do, so the magnifier has to be what identifies it.
    cx, cy = int(n * 0.630), int(n * 0.655)
    r = int(n * 0.200)                   # glass radius
    ring = int(n * 0.048)                # ring thickness
    hand_w = int(n * 0.066)

    def circle(d, x, y, rad, **kw):
        d.ellipse((x - rad, y - rad, x + rad, y + rad), **kw)

    # A shadow first, so the mark separates from a very busy reef.
    shadow = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    ds = ImageDraw.Draw(shadow)
    circle(ds, cx, cy, r + ring, fill=(0, 0, 0, 150))
    # The handle stops short of the canvas edge on purpose: run it to the
    # corner and the round cap is sliced off, which at 32 pixels reads as a
    # rendering fault rather than a handle.
    hx, hy = cx + int(r * 0.78), cy + int(r * 0.78)
    ex, ey = cx + int(r * 1.66), cy + int(r * 1.66)
    ds.line((hx, hy, ex, ey), fill=(0, 0, 0, 150), width=hand_w + ring)
    shadow = shadow.filter(ImageFilter.GaussianBlur(n * 0.012))
    canvas = Image.alpha_composite(canvas, shadow)

    mark = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(mark)

    # handle: dark core with a white rim, so it reads on light and dark alike
    d.line((hx, hy, ex, ey), fill=INK, width=hand_w + int(ring * 0.7))
    d.line((hx, hy, ex, ey), fill=RIM, width=hand_w)
    circle(d, ex, ey, (hand_w + int(ring * 0.7)) // 2, fill=INK)
    circle(d, ex, ey, hand_w // 2, fill=RIM)

    # glass: lets the reef through, but lifted and cooled so the ring reads
    glass = Image.new("RGBA", (n, n), (0, 0, 0, 0))
    dg = ImageDraw.Draw(glass)
    circle(dg, cx, cy, r, fill=(224, 244, 255, 122))
    mark = Image.alpha_composite(mark, glass)
    d = ImageDraw.Draw(mark)

    # ring: white between two dark strokes
    circle(d, cx, cy, r + ring // 2 + int(ring * 0.22), outline=INK,
           width=max(2, int(ring * 0.30)))
    circle(d, cx, cy, r + ring // 2, outline=RIM, width=ring)
    circle(d, cx, cy, r - int(ring * 0.22), outline=INK,
           width=max(2, int(ring * 0.30)))

    # Two short parallel strokes, thin and angled: the conventional way to
    # say "glass". One thick straight bar - the first attempt - read as a
    # white label stuck to the lens.
    for scale, alpha in ((1.0, 225), (0.52, 170)):
        ox = int(r * (1.0 - scale) * 0.55)
        d.line((cx - int(r * 0.52) + ox, cy - int(r * 0.16) - ox,
                cx - int(r * (0.52 - 0.40 * scale)) + ox,
                cy - int(r * (0.16 + 0.40 * scale)) - ox),
               fill=(255, 255, 255, alpha),
               width=max(2, int(ring * 0.40)))

    canvas = Image.alpha_composite(canvas, mark)
    return canvas.resize((256, 256), Image.LANCZOS)


# --------------------------------------------------------------------------
# ICO, written by hand so that every entry is a DIB
# --------------------------------------------------------------------------

def dib_entry(im) -> bytes:
    """One icon image as BITMAPINFOHEADER + BGRA bottom-up + AND mask."""
    w, h = im.size
    px = im.load()
    header = struct.pack("<IiiHHIIiiII",
                         40,          # biSize
                         w, h * 2,    # biWidth, biHeight (XOR + AND)
                         1, 32,       # biPlanes, biBitCount
                         0,           # biCompression = BI_RGB
                         w * h * 4,   # biSizeImage
                         0, 0, 0, 0)
    body = bytearray()
    for y in range(h - 1, -1, -1):                 # bottom-up
        for x in range(w):
            r, g, b, a = px[x, y]
            body += bytes((b, g, r, a))
    # The AND mask is unused for 32bpp, but must be present and row-padded
    # to four bytes. Leaving it out produces a file that Explorer accepts and
    # Tk silently refuses - which is the worst of both.
    row = ((w + 31) // 32) * 4
    mask = bytes(row * h)
    return bytes(header) + bytes(body) + mask


def write_ico(path: Path, master, sizes=ICO_SIZES) -> list:
    from PIL import Image
    entries = []
    for s in sizes:
        im = master.resize((s, s), Image.LANCZOS)
        entries.append((s, dib_entry(im)))
    out = bytearray(struct.pack("<HHH", 0, 1, len(entries)))
    offset = 6 + 16 * len(entries)
    for s, blob in entries:
        out += struct.pack("<BBBBHHII",
                           0 if s >= 256 else s, 0 if s >= 256 else s,
                           0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for _s, blob in entries:
        out += blob
    path.write_bytes(bytes(out))
    return [s for s, _ in entries]


def verify_ico(path: Path) -> list:
    """Every entry must be a DIB. Tk cannot read a PNG-compressed one."""
    d = path.read_bytes()
    _res, _typ, n = struct.unpack("<HHH", d[:6])
    report = []
    for i in range(n):
        off = 6 + i * 16
        w, h, _c, _r, _p, bpp, size, offset = struct.unpack(
            "<BBBBHHII", d[off:off + 16])
        blob = d[offset:offset + size]
        if blob[:8] == b"\x89PNG\r\n\x1a\n":
            kind = "PNG"
        else:
            kind = "DIB" if struct.unpack("<I", blob[:4])[0] == 40 else "?"
        report.append((w or 256, bpp, size, kind))
    return report


def main() -> int:
    if not SOURCE.exists():
        print(f"no source logo at {SOURCE}")
        return 1
    try:
        master = build(SOURCE)
    except ImportError:
        print("Pillow is needed to rebuild the icon: pip install pillow")
        return 1

    BRAND.mkdir(parents=True, exist_ok=True)
    from PIL import Image

    big = BRAND / "graphvis-audit.png"
    master.save(big)
    print(f"wrote {big.name}  256x256")

    badge = BRAND / "graphvis-audit-badge.png"
    master.resize((BADGE, BADGE), Image.LANCZOS).save(badge)
    print(f"wrote {badge.name}  {BADGE}x{BADGE}")

    ico = BRAND / "graphvis-audit.ico"
    sizes = write_ico(ico, master)
    report = verify_ico(ico)
    print(f"wrote {ico.name}  {', '.join(str(s) for s in sizes)}")
    bad = [r for r in report if r[3] != "DIB"]
    for w, bpp, size, kind in report:
        print(f"    {w:>3}x{w:<3} {bpp}bpp {size:>7}B  {kind}")
    if bad:
        print("FAILED: some entries are not DIB; Tk on Windows will refuse them")
        return 1
    print("every entry is a DIB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
