#!/usr/bin/env python3
"""Generate the 192x32 dice face texture atlas (6 faces, 32x32 each).

Output: src/assets/dice_atlas.png

Requires: Pillow  (pip install Pillow)
"""
import os
from PIL import Image, ImageDraw

FACE_SIZE = 32
NUM_FACES = 6
PIP_R = 3
BG_COLOR = (200, 30, 30)
PIP_COLOR = (255, 255, 255)

# Pip centre positions on a 32x32 face
TL = (9, 9);   TC = (16, 9);  TR = (23, 9)
ML = (9, 16);  MC = (16, 16); MR = (23, 16)
BL = (9, 23);  BC = (16, 23); BR = (23, 23)

PIP_LAYOUTS = [
    [MC],                        # face 1
    [TR, BL],                    # face 2
    [TR, MC, BL],                # face 3
    [TL, TR, BL, BR],            # face 4
    [TL, TR, MC, BL, BR],        # face 5
    [TL, TR, ML, MR, BL, BR],   # face 6
]


def main():
    W, H = FACE_SIZE, FACE_SIZE
    atlas = Image.new("RGB", (W * NUM_FACES, H), BG_COLOR)
    draw = ImageDraw.Draw(atlas)

    for idx, pips in enumerate(PIP_LAYOUTS):
        x0 = idx * W
        # subtle border
        draw.rectangle([x0 + 1, 1, x0 + W - 2, H - 2], outline=(150, 20, 20))
        draw.rectangle([x0 + 2, 2, x0 + W - 3, H - 3], outline=(180, 25, 25))

        for cx, cy in pips:
            draw.ellipse(
                [x0 + cx - PIP_R, cy - PIP_R, x0 + cx + PIP_R, cy + PIP_R],
                fill=PIP_COLOR,
            )

    out_dir = os.path.join(os.path.dirname(__file__), "..", "src", "assets")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "dice_atlas.png")
    atlas.save(out_path)
    print(f"wrote {out_path}  ({atlas.size[0]}x{atlas.size[1]})")


if __name__ == "__main__":
    main()
