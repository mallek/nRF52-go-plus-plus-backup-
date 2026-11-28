#!/usr/bin/env python3
"""Generate pp_gachi_sprites.generated.h from fan-made sprite sheets.

  python tools/sprites/gen_sprites.py OUT_HEADER [--cache DIR]
  python tools/sprites/gen_sprites.py OUT_HEADER --dummy

Normal mode reads the animation sheets of the PMD SpriteCollab project from the
cache folder and downloads any that are missing. --dummy needs no network and
reads no files: it writes plain animated circles.

The art is CC BY-NC (credits: https://sprites.pmdcollab.org/). It is never
committed: the cache and the generated header are gitignored.
"""

import argparse
import io
import os
import sys
import urllib.request
import xml.etree.ElementTree as ET

from PIL import Image

DEX_LIST = [1, 2, 3, 4, 5, 6, 7, 8, 9, 25, 26]
BASE_URL = "https://raw.githubusercontent.com/PMDCollab/SpriteCollab/master/sprite/"
DEFAULT_CACHE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "cache")
TICK_MS = 1000 / 60            # one tick of the sheets' durations
ALPHA_CUTOFF = 128
MAX_COLORS = 15

# In the order of PpStageAnim in the firmware, and the sheet row each one uses.
ANIM_NAMES = ["Idle", "Hop", "Sleep", "Eat", "Walk"]
ANIM_ROW = {"Idle": 0, "Hop": 0, "Sleep": 0, "Eat": 0, "Walk": 2}


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def ticks_to_ms(ticks):
    return int(round(ticks * TICK_MS))


def _flat(im):
    getter = getattr(im, "get_flattened_data", None)
    return list(getter() if getter else im.getdata())


# ---------------------------------------------------------------- AnimData

def parse_anim_data(xml_text):
    """Returns {name: {"w", "h", "ticks", "sheet"}}. CopyOf is resolved: size and
    ticks come from the animation it names, and "sheet" is that animation's name."""
    root = ET.fromstring(xml_text)
    raw = {}
    for anim in root.iter("Anim"):
        name = (anim.findtext("Name") or "").strip()
        if not name:
            continue
        copy = (anim.findtext("CopyOf") or "").strip()
        if copy:
            raw[name] = {"copy": copy}
        else:
            ticks = [int(d.text) for d in anim.iter("Duration")]
            raw[name] = {
                "w": int(anim.findtext("FrameWidth")),
                "h": int(anim.findtext("FrameHeight")),
                "ticks": ticks,
                "sheet": name,
            }
    out = {}
    for name in raw:
        cur = name
        for _ in range(8):
            if "copy" not in raw.get(cur, {}):
                break
            cur = raw[cur]["copy"]
        entry = raw.get(cur)
        if entry is None or "copy" in entry:
            continue
        out[name] = {"w": entry["w"], "h": entry["h"], "ticks": list(entry["ticks"]), "sheet": cur}
    return out


def slice_frames(sheet, frame_w, frame_h, row):
    """The frames of one row of a sheet, left to right. A sheet without that row uses row 0."""
    sheet = sheet.convert("RGBA")
    rows = max(1, sheet.height // frame_h)
    if row >= rows:
        row = 0
    count = sheet.width // frame_w
    return [sheet.crop((i * frame_w, row * frame_h, (i + 1) * frame_w, (row + 1) * frame_h))
            for i in range(count)]


# ----------------------------------------------------------------- palette

def _opaque(img):
    """Yields (x, y, rgb) of the opaque pixels."""
    img = img.convert("RGBA")
    pix = _flat(img)
    w = img.width
    for i, p in enumerate(pix):
        if p[3] >= ALPHA_CUTOFF:
            yield i % w, i // w, p[:3]


def _palette_map(images):
    """Returns (palette, lookup): palette[0] is None (transparent), then at most 15 colors
    in first-seen order; lookup maps every opaque color to its index."""
    distinct = []
    seen = set()
    for img in images:
        for _x, _y, c in _opaque(img):
            if c not in seen:
                seen.add(c)
                distinct.append(c)
    if len(distinct) <= MAX_COLORS:
        return [None] + distinct, {c: i + 1 for i, c in enumerate(distinct)}
    print("  reducing %d colors to %d" % (len(distinct), MAX_COLORS))
    strip = Image.new("RGB", (len(distinct), 1))
    for i, c in enumerate(distinct):
        strip.putpixel((i, 0), c)
    q = strip.quantize(colors=MAX_COLORS, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    qpal = q.getpalette()
    used = sorted(set(_flat(q)))
    remap = {p: i + 1 for i, p in enumerate(used)}
    colors = [tuple(qpal[p * 3:p * 3 + 3]) for p in used]
    lookup = {c: remap[q.getpixel((i, 0))] for i, c in enumerate(distinct)}
    return [None] + colors, lookup


def build_palette(images):
    """One shared palette for a set of images: [None] plus at most 15 RGB tuples."""
    return _palette_map(images)[0]


def _lookup_for(palette, images):
    """Index lookup for pack_animation when only a palette was given: exact match, else nearest."""
    exact = {c: i for i, c in enumerate(palette) if c is not None}
    lookup = dict(exact)
    for img in images:
        for _x, _y, c in _opaque(img):
            if c not in lookup:
                best = min(exact, key=lambda k: sum((k[j] - c[j]) ** 2 for j in range(3)))
                lookup[c] = exact[best]
    return lookup


# --------------------------------------------------------------- animation

def pack_animation(frames, palette=None):
    """Crops all frames to the union of their drawn pixels and indexes them.
    Returns {"w", "h", "origin_x", "baseline", "frames": [rows of palette indices]}.
    origin_x: columns from the crop's left edge to the frame's centre.
    baseline: rows from the crop's top to the lowest drawn row plus one."""
    frames = [f.convert("RGBA") for f in frames]
    if palette is None:
        palette, lookup = _palette_map(frames)
    else:
        lookup = _lookup_for(palette, frames)
    left = top = None
    right = bottom = None
    for f in frames:
        for x, y, _c in _opaque(f):
            left = x if left is None else min(left, x)
            right = x if right is None else max(right, x)
            top = y if top is None else min(top, y)
            bottom = y if bottom is None else max(bottom, y)
    if left is None:
        return {"w": 1, "h": 1, "origin_x": 0, "baseline": 1, "frames": [[[0]] for _ in frames]}
    w = right - left + 1
    h = bottom - top + 1
    out = []
    for f in frames:
        grid = [[0] * w for _ in range(h)]
        for x, y, c in _opaque(f):
            grid[y - top][x - left] = lookup[c]
        out.append(grid)
    return {
        "w": w,
        "h": h,
        "origin_x": frames[0].width // 2 - left,
        "baseline": bottom + 1 - top,
        "frames": out,
    }


def same_indices(a, b):
    """True when two {name: pack or None} maps have identical pixel indices everywhere."""
    for name in set(a) | set(b):
        pa, pb = a.get(name), b.get(name)
        if (pa is None) != (pb is None):
            return False
        if pa is None:
            continue
        if (pa["w"], pa["h"], pa["origin_x"], pa["baseline"]) != (pb["w"], pb["h"], pb["origin_x"], pb["baseline"]):
            return False
        if pa["frames"] != pb["frames"]:
            return False
    return True


def build_sprite(raw):
    """raw: {"dex", "shiny", "frames": {name: [images] or None}, "ms": {name: [ms] or None}}.
    Returns {"dex", "shiny", "palette": 16 RGB565, "anim": {name: pack with "ms", or None},
    "shares": False}."""
    all_images = []
    for name in ANIM_NAMES:
        if raw["frames"].get(name):
            all_images.extend(raw["frames"][name])
    palette, lookup = _palette_map(all_images)
    anims = {}
    for name in ANIM_NAMES:
        imgs = raw["frames"].get(name)
        if not imgs:
            anims[name] = None
            continue
        pack = pack_animation(imgs, palette=palette)
        ms = list(raw["ms"][name])
        while len(ms) < len(imgs):
            ms.append(ms[-1] if ms else 100)
        pack["ms"] = ms[:len(imgs)]
        anims[name] = pack
    pal565 = [0] + [rgb565(*c) for c in palette[1:]]
    pal565 += [0] * (16 - len(pal565))
    return {"dex": raw["dex"], "shiny": raw["shiny"], "palette": pal565, "anim": anims, "shares": False}


def link_shiny(normal, shiny):
    """Marks the shiny as sharing the normal's pixel arrays when every index matches."""
    shiny["shares"] = same_indices(normal["anim"], shiny["anim"])
    return shiny["shares"]


# ------------------------------------------------------------------- dummy

def _dummy_color(dex, shiny):
    hue = (dex * 47) % 256
    base = ((hue * 3) % 200 + 40, (hue * 5) % 200 + 40, (hue * 7) % 200 + 40)
    if shiny:
        base = (255 - base[0] // 2, 255 - base[1] // 2, 80 + base[2] // 4)
    return base + (255,)


def _ellipse(color, cx, cy, rx, ry):
    img = Image.new("RGBA", (32, 40), (0, 0, 0, 0))
    for y in range(40):
        for x in range(32):
            ex = (x + 0.5 - cx) / rx
            ey = (y + 0.5 - cy) / ry
            if ex * ex + ey * ey <= 1.0:
                img.putpixel((x, y), color)
    return img


def dummy_sprite(dex, shiny):
    """A packed placeholder sprite: a circle that squashes, hops, lies flat, bobs and leans."""
    c = _dummy_color(dex, shiny)
    ground = 36.0
    frames = {
        "Idle": [_ellipse(c, 16, ground - 11, 11, 11), _ellipse(c, 16, ground - 9, 12, 9)],
        "Hop": [_ellipse(c, 16, ground - 11 - lift, 11, 11) for lift in (0, 8, 14, 6)],
        "Sleep": [_ellipse(c, 16, ground - 6, 13, 6), _ellipse(c, 16, ground - 7, 13, 7)],
        "Eat": [_ellipse(c, 16, ground - 11, 11, 11), _ellipse(c, 16, ground - 8, 11, 8)],
        "Walk": [_ellipse(c, 14, ground - 11, 11, 11), _ellipse(c, 18, ground - 11, 11, 11)],
    }
    ms = {"Idle": [500, 500], "Hop": [120, 120, 120, 120], "Sleep": [900, 900],
          "Eat": [250, 250], "Walk": [200, 200]}
    return build_sprite({"dex": dex, "shiny": shiny, "frames": frames, "ms": ms})


# ------------------------------------------------------------------ header

def _array(name, ctype, values, fmt):
    return "static const %s %s[] = {%s};" % (ctype, name, ", ".join(fmt % v for v in values))


def _pack_bytes(anim):
    stride = (anim["w"] + 1) // 2
    out = bytearray()
    for grid in anim["frames"]:
        for row in grid:
            line = bytearray(stride)
            for x, idx in enumerate(row):
                line[x // 2] |= (idx << 4) if x % 2 == 0 else idx
            out.extend(line)
    return list(out)


PIXEL_BYTES_WARN = 450000


def pixel_bytes(sprites):
    """(total pixel-data bytes written, shiny sprites sharing the normal's arrays)."""
    total = 0
    shared = 0
    for sprite in sprites:
        if sprite.get("shares"):
            shared += 1
            continue
        for anim in sprite["anim"].values():
            if anim is not None:
                total += len(_pack_bytes(anim))
    return total, shared


def render_header(sprites, dummy=False):
    """sprites: list of dicts from build_sprite / dummy_sprite (after link_shiny)."""
    lines = []
    if dummy:
        lines.append("// DUMMY sprite table: placeholder circles, no game art.")
    else:
        lines.append("// Generated by tools/sprites/gen_sprites.py. Do not edit, do not commit.")
    lines.append("#pragma once")
    lines.append("")
    lines.append("#include <stdint.h>")
    lines.append("")
    owner = {}   # (dex, name) -> sprite index that owns the normal arrays
    for n, sp in enumerate(sprites):
        lines.append(_array("PP_GACHI_PAL_%d" % n, "uint16_t", sp["palette"], "0x%04X"))
        for name in ANIM_NAMES:
            a = sp["anim"][name]
            if a is None:
                continue
            if sp["shares"] and (sp["dex"], name) in owner:
                continue
            if not sp["shiny"]:
                owner[(sp["dex"], name)] = n
            lines.append(_array("PP_GACHI_MS_%d_%s" % (n, name), "uint16_t", a["ms"], "%d"))
            lines.append(_array("PP_GACHI_PIX_%d_%s" % (n, name), "uint8_t", _pack_bytes(a), "0x%02X"))
    lines.append("")
    lines.append("static const PpGachiSprite PP_GACHI_SPRITE_TABLE[] = {")
    for n, sp in enumerate(sprites):
        entries = []
        for name in ANIM_NAMES:
            a = sp["anim"][name]
            if a is None:
                entries.append("{0, 0, 0, 0, 0, nullptr, nullptr}")
                continue
            src = owner[(sp["dex"], name)] if sp["shares"] else n
            entries.append("{%d, %d, %d, %d, %d, PP_GACHI_MS_%d_%s, PP_GACHI_PIX_%d_%s}" % (
                a["w"], a["h"], len(a["frames"]), a["origin_x"], a["baseline"], src, name, src, name))
        lines.append("  {%d, %s, PP_GACHI_PAL_%d, {" % (sp["dex"], "true" if sp["shiny"] else "false", n))
        for e in entries:
            lines.append("    " + e + ",")
        lines.append("  }},  // dex %d%s" % (sp["dex"], " shiny" if sp["shiny"] else ""))
    if not sprites:
        lines.append("  {0, false, nullptr, {}},")
    lines.append("};")
    lines.append("static const uint8_t PP_GACHI_SPRITE_TABLE_COUNT = %d;" % len(sprites))
    lines.append("")
    return "\n".join(lines)


# ---------------------------------------------------------------- download

def _fetch(url):
    with urllib.request.urlopen(url, timeout=20) as r:
        return r.read()


def _cached(cache_dir, sub, filename, url, fetch):
    """Bytes of a file from the cache, downloading it into the cache first when missing."""
    path = os.path.join(cache_dir, "pmd", sub, filename)
    if os.path.exists(path):
        with open(path, "rb") as f:
            return f.read()
    data = fetch(url)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)
    return data


def load_raw_sprite(dex, shiny, cache_dir, fetch=_fetch):
    """Frames and times of every animation of one species and variant, or None when its
    AnimData cannot be had. An animation that is absent or fails leaves None and prints a line."""
    dex4 = "%04d" % dex
    sub = dex4 + ("/shiny" if shiny else "")
    url_dir = BASE_URL + dex4 + ("/0000/0001/" if shiny else "/")
    label = "dex %d%s" % (dex, " shiny" if shiny else "")
    try:
        data = _cached(cache_dir, sub, "AnimData.xml", url_dir + "AnimData.xml", fetch)
        info = parse_anim_data(data.decode("utf-8"))
    except Exception as e:
        print("failed: %s AnimData (%s)" % (label, e))
        return None
    frames = {}
    ms = {}
    for name in ANIM_NAMES:
        frames[name] = None
        ms[name] = None
        meta = info.get(name)
        if meta is None:
            print("missing: %s has no %s" % (label, name))
            continue
        fname = "%s-Anim.png" % meta["sheet"]
        try:
            png = _cached(cache_dir, sub, fname, url_dir + fname, fetch)
            with Image.open(io.BytesIO(png)) as im:
                im.load()
                got = slice_frames(im, meta["w"], meta["h"], ANIM_ROW[name])
        except Exception as e:
            print("failed: %s %s (%s)" % (label, fname, e))
            continue
        if not got:
            print("failed: %s %s (no frames)" % (label, fname))
            continue
        times = [ticks_to_ms(t) for t in meta["ticks"]] or [100]
        frames[name] = got
        ms[name] = times
    return {"dex": dex, "shiny": shiny, "frames": frames, "ms": ms}


def collect_sprites(cache_dir, fetch=_fetch):
    """Builds every sprite. A species without Idle is left out and reported."""
    sprites = []
    for dex in DEX_LIST:
        built = {}
        for shiny in (False, True):
            raw = load_raw_sprite(dex, shiny, cache_dir, fetch)
            if raw is None or not raw["frames"]["Idle"]:
                print("left out: dex %d%s has no Idle" % (dex, " shiny" if shiny else ""))
                continue
            print("packing dex %d%s" % (dex, " shiny" if shiny else ""))
            built[shiny] = build_sprite(raw)
        if False in built:
            sprites.append(built[False])
            if True in built:
                link_shiny(built[False], built[True])
                sprites.append(built[True])
        elif True in built:
            print("left out: dex %d shiny has no normal sprite" % dex)
    return sprites


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("out_header")
    ap.add_argument("--cache", default=DEFAULT_CACHE)
    ap.add_argument("--dummy", action="store_true")
    args = ap.parse_args(argv)
    if args.dummy:
        sprites = []
        for d in DEX_LIST:
            n = dummy_sprite(d, False)
            s = dummy_sprite(d, True)
            link_shiny(n, s)
            sprites.extend([n, s])
    else:
        sprites = collect_sprites(args.cache)
    if not sprites:
        print("no sprites written")
        return 1
    with open(args.out_header, "w", newline="\n") as f:
        f.write(render_header(sprites, dummy=args.dummy))
    print("wrote %d sprites" % len(sprites))
    total, shared = pixel_bytes(sprites)
    print("pixel data: %d bytes; %d shiny sprites share pixel arrays with their normal sprite"
          % (total, shared))
    if total > PIXEL_BYTES_WARN:
        print("WARNING: pixel data is over %d bytes (about 550 KB of flash is free for art)."
              % PIXEL_BYTES_WARN)
    print("Real-art flash use is unmeasured: compile once with the real art before flashing.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
