"""Converts every .ppm in a folder to a .png next to it, enlarged with nearest
neighbour so single pixels stay sharp. Needs Pillow.

    python -I tests/preview/ppm_to_png.py <dir> [scale]
"""
import os
import sys

from PIL import Image


def main(argv):
    if len(argv) < 2:
        print("usage: ppm_to_png.py <dir> [scale]")
        return 2
    folder = argv[1]
    scale = int(argv[2]) if len(argv) > 2 else 2
    count = 0
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".ppm"):
            continue
        img = Image.open(os.path.join(folder, name))
        if scale != 1:
            img = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
        img.save(os.path.join(folder, name[:-4] + ".png"))
        count += 1
    print("converted %d file(s)" % count)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
