"""Generate app.ico: the same crescent the app draws at runtime.

Pure stdlib. Writes a multi-size BMP-format ICO with 4x4 supersampled
coverage so the mark stays clean at 16px.
"""
import math
import struct
import os
import sys

SIZES = [16, 20, 24, 32, 48, 64]          # a tray app never needs 256px
R, G, B = 0x9F, 0xB9, 0x3C          # default accent (a green lifted for dark backgrounds)
if len(sys.argv) > 1:               # python make_icon.py 2E7D32  -> your own colour
    h = sys.argv[1].lstrip("#")
    R, G, B = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)


def crescent(size):
    """Return BGRA rows (bottom-up) for one icon size."""
    s = float(size)
    ocx, ocy, orad = 0.50 * s, 0.50 * s, 0.45 * s
    icx, icy, irad = 0.66 * s, 0.38 * s, 0.38 * s

    rows = []
    for y in range(size - 1, -1, -1):        # BMP rows run bottom-up
        row = bytearray()
        for x in range(size):
            hits = 0
            for sy in range(4):
                for sx in range(4):
                    px = x + (sx + 0.5) / 4.0
                    py = y + (sy + 0.5) / 4.0
                    d1 = math.hypot(px - ocx, py - ocy)
                    d2 = math.hypot(px - icx, py - icy)
                    if d1 <= orad and d2 > irad:
                        hits += 1
            a = hits / 16.0
            # ICO colour data is straight (non-premultiplied) alpha
            row += bytes((B, G, R, int(a * 255 + 0.5))) if a > 0 else b"\0\0\0\0"
        rows.append(bytes(row))
    return b"".join(rows)


def ico_image(size):
    """One BITMAPINFOHEADER image: header + BGRA pixels + AND mask."""
    pixels = crescent(size)
    hdr = struct.pack(
        "<IiiHHIIiiII",
        40,            # biSize
        size,          # biWidth
        size * 2,      # biHeight (colour + mask, per ICO convention)
        1,             # biPlanes
        32,            # biBitCount
        0,             # biCompression
        len(pixels),   # biSizeImage
        0, 0, 0, 0,
    )
    # AND mask: one bit per pixel, rows padded to 4 bytes. All zero = use alpha.
    stride = ((size + 31) // 32) * 4
    mask = b"\0" * (stride * size)
    return hdr + pixels + mask


def main():
    out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "app.ico")
    out = os.path.normpath(out)

    images = [(s, ico_image(s)) for s in SIZES]

    header = struct.pack("<HHH", 0, 1, len(images))
    offset = 6 + 16 * len(images)
    entries, blobs = b"", b""
    for size, data in images:
        entries += struct.pack(
            "<BBBBHHII",
            0 if size >= 256 else size,   # 0 means 256
            0 if size >= 256 else size,
            0, 0, 1, 32,
            len(data), offset,
        )
        blobs += data
        offset += len(data)

    with open(out, "wb") as f:
        f.write(header + entries + blobs)
    print("wrote %s (%d bytes, %d sizes)" % (out, 6 + 16 * len(images) + len(blobs), len(images)))


if __name__ == "__main__":
    main()
