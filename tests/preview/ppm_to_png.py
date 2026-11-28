"""Turns the preview output into images. Needs Pillow.

    python -I tests/preview/ppm_to_png.py <dir> [scale]

- Every .ppm in <dir> becomes a .png next to it.
- Every gif_<name>/ folder (frames plus frames.txt, "file ms" per line, written by
  the preview) becomes <name>.gif in <dir>, and <name>_sheet.png: frames sampled at
  even times through the animation, side by side, for a quick look.

Images are enlarged with nearest neighbour (default 2x) so single pixels stay sharp.
"""
import os
import sys

from PIL import Image

SHEET_SAMPLES = 12
SHEET_COLUMNS = 6


def enlarge(img, scale):
    if scale == 1:
        return img
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST)


def read_frames(folder):
    frames = []
    with open(os.path.join(folder, "frames.txt"), encoding="ascii") as f:
        for line in f:
            parts = line.split()
            if len(parts) != 2:
                continue
            img = Image.open(os.path.join(folder, parts[0])).convert("RGB")
            frames.append((img, int(parts[1])))
    return frames


def frame_at(frames, t):
    acc = 0
    for img, ms in frames:
        acc += ms
        if t < acc:
            return img
    return frames[-1][0]


def contact_sheet(frames, samples, columns):
    total = sum(ms for _, ms in frames)
    count = 1 if len(frames) == 1 else samples
    picks = [frame_at(frames, total * i // count) for i in range(count)]
    w, h = picks[0].size
    gap = 4
    cols = min(columns, count)
    rows = (count + cols - 1) // cols
    sheet = Image.new("RGB", (cols * w + (cols - 1) * gap, rows * h + (rows - 1) * gap), (90, 90, 90))
    for i, img in enumerate(picks):
        sheet.paste(img, ((i % cols) * (w + gap), (i // cols) * (h + gap)))
    return sheet


def make_gif(folder, out_dir, name, scale):
    frames = read_frames(folder)
    if not frames:
        return False
    images = [enlarge(img, scale).convert("P", palette=Image.ADAPTIVE, colors=255) for img, _ in frames]
    durations = [ms for _, ms in frames]
    first, rest = images[0], images[1:]
    first.save(os.path.join(out_dir, name + ".gif"), save_all=True, append_images=rest,
               duration=durations if len(durations) > 1 else durations[0], loop=0, optimize=False)
    contact_sheet(frames, SHEET_SAMPLES, SHEET_COLUMNS).save(os.path.join(out_dir, name + "_sheet.png"))
    return True


def main(argv):
    if len(argv) < 2:
        print("usage: ppm_to_png.py <dir> [scale]")
        return 2
    folder = argv[1]
    scale = int(argv[2]) if len(argv) > 2 else 2
    count = 0
    gifs = 0
    for name in sorted(os.listdir(folder)):
        path = os.path.join(folder, name)
        if name.endswith(".ppm") and os.path.isfile(path):
            img = Image.open(path)
            enlarge(img, scale).save(os.path.join(folder, name[:-4] + ".png"))
            count += 1
        elif name.startswith("gif_") and os.path.isdir(path):
            if make_gif(path, folder, name[4:], scale):
                gifs += 1
    print("converted %d file(s), assembled %d GIF(s)" % (count, gifs))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
