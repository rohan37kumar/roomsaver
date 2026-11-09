"""
Builds src/core/roomsaver.ico (the exe/screensaver icon) from a RoomSaver render:
the isometric room cut out along its hexagon outline on a transparent background.

1. Render the bundled room at the default selftest size (1280x720), idle TV:
       set ROOMSAVER_TV_IDLE=1
       set ROOMSAVER_SELFTEST_DELAY_MS=800
       build\\Release\\RoomSaver.exe --selftest=build\\IconSrc.png
2. python tools/make_icon.py build/IconSrc.png src/core/roomsaver.ico

Requires Pillow. The hexagon below is the room's outline in that 1280x720 render
of the bundled assets/room.glb; re-measure it if the room or camera changes.
"""

import sys
from PIL import Image, ImageDraw

# Room outline (outer wall edges) in the 1280x720 selftest render.
HEXAGON = [(640, 58), (946, 236), (946, 480), (640, 660), (335, 480), (335, 236)]
SIZES = [256, 128, 64, 48, 32, 24, 16]
SUPERSAMPLE = 4  # anti-aliased mask edge


def build_icon_image(src: Image.Image) -> Image.Image:
    xs = [p[0] for p in HEXAGON]
    ys = [p[1] for p in HEXAGON]
    x0, x1, y0, y1 = min(xs), max(xs), min(ys), max(ys)
    side = max(x1 - x0, y1 - y0) + 4             # small transparent margin
    left = (x0 + x1) // 2 - side // 2
    top = (y0 + y1) // 2 - side // 2
    crop = src.convert("RGBA").crop((left, top, left + side, top + side))

    # Supersampled polygon mask, inset by ~1px so no backdrop bleeds at the edge.
    cx, cy = side / 2, side / 2
    pts = []
    for (x, y) in HEXAGON:
        px, py = x - left, y - top
        dx, dy = px - cx, py - cy
        length = max((dx * dx + dy * dy) ** 0.5, 1e-6)
        px -= dx / length
        py -= dy / length
        pts.append((px * SUPERSAMPLE, py * SUPERSAMPLE))
    mask = Image.new("L", (side * SUPERSAMPLE, side * SUPERSAMPLE), 0)
    ImageDraw.Draw(mask).polygon(pts, fill=255)
    mask = mask.resize((side, side), Image.LANCZOS)

    crop.putalpha(mask)
    return crop


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    src = Image.open(sys.argv[1])
    if src.size != (1280, 720):
        print(f"expected a 1280x720 selftest render, got {src.size}")
        return 1
    icon = build_icon_image(src).resize((256, 256), Image.LANCZOS)
    icon.save(sys.argv[2], format="ICO", sizes=[(s, s) for s in SIZES])
    print(f"wrote {sys.argv[2]} ({', '.join(str(s) for s in SIZES)} px)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
