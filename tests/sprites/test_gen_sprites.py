import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "sprites"))

from PIL import Image  # noqa: E402

import gen_sprites  # noqa: E402
from gen_sprites import (  # noqa: E402
    build_palette, dummy_sprite, pack_animation, parse_anim_data, render_header,
    rgb565, same_indices, slice_frames,
)

XML = """<?xml version="1.0"?>
<AnimData><ShadowSize>1</ShadowSize><Anims>
<Anim><Name>Idle</Name><Index>0</Index><FrameWidth>32</FrameWidth><FrameHeight>40</FrameHeight>
<Durations><Duration>12</Duration><Duration>8</Duration><Duration>8</Duration><Duration>8</Duration></Durations></Anim>
<Anim><Name>Hop</Name><Index>1</Index><FrameWidth>32</FrameWidth><FrameHeight>88</FrameHeight>
<Durations><Duration>4</Duration><Duration>4</Duration></Durations></Anim>
<Anim><Name>Sleep</Name><CopyOf>Idle</CopyOf></Anim>
</Anims></AnimData>
"""

RED = (255, 0, 0, 255)
GREEN = (0, 255, 0, 255)
BLUE = (0, 0, 255, 255)
CLEAR = (0, 0, 0, 0)


def blank(w=32, h=40):
    return Image.new("RGBA", (w, h), CLEAR)


def fill(img, x0, x1, y0, y1, color):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            img.putpixel((x, y), color)


def test_rgb565():
    assert rgb565(255, 255, 255) == 0xFFFF
    assert rgb565(0, 0, 0) == 0
    assert rgb565(255, 0, 0) == 0xF800
    assert rgb565(0, 255, 0) == 0x07E0
    assert rgb565(0, 0, 255) == 0x001F


def test_parse_anim_data():
    d = parse_anim_data(XML)
    assert d["Idle"]["w"] == 32 and d["Idle"]["h"] == 40
    assert d["Idle"]["ticks"] == [12, 8, 8, 8]
    assert d["Hop"]["w"] == 32 and d["Hop"]["h"] == 88
    assert d["Hop"]["ticks"] == [4, 4]


def test_parse_copy_of():
    d = parse_anim_data(XML)
    assert d["Sleep"]["w"] == 32 and d["Sleep"]["h"] == 40
    assert d["Sleep"]["ticks"] == [12, 8, 8, 8]
    assert d["Sleep"]["sheet"] == "Idle"
    assert d["Idle"]["sheet"] == "Idle"


def test_ticks_to_ms():
    assert gen_sprites.ticks_to_ms(12) == 200
    assert gen_sprites.ticks_to_ms(8) == 133
    assert abs(gen_sprites.TICK_MS - 1000 / 60) < 1e-9


def test_slice_rows():
    sheet = Image.new("RGBA", (64, 8 * 10), CLEAR)
    for row in range(8):
        fill(sheet, 0, 3, row * 10, row * 10 + 3, (row * 30, 10, 10, 255))
        fill(sheet, 32, 35, row * 10, row * 10 + 3, (row * 30, 99, 10, 255))
    f0 = slice_frames(sheet, 32, 10, 0)
    f2 = slice_frames(sheet, 32, 10, 2)
    assert len(f0) == 2 and len(f2) == 2
    assert f0[0].size == (32, 10)
    assert f0[0].getpixel((1, 1)) == (0, 10, 10, 255)
    assert f2[0].getpixel((1, 1)) == (60, 10, 10, 255)
    assert f2[1].getpixel((1, 1)) == (60, 99, 10, 255)
    one = Image.new("RGBA", (96, 10), CLEAR)
    fill(one, 64, 67, 0, 3, RED)
    f = slice_frames(one, 32, 10, 2)   # one row: row 2 does not exist, row 0 is used
    assert len(f) == 3
    assert f[2].getpixel((1, 1)) == RED
    assert len(slice_frames(one, 32, 10, 0)) == 3


def test_pack_crops_union():
    a = blank()
    b = blank()
    fill(a, 5, 8, 10, 12, RED)
    fill(b, 20, 25, 20, 30, RED)
    p = pack_animation([a, b])
    assert p["w"] == 25 - 5 + 1
    assert p["h"] == 30 - 10 + 1
    assert len(p["frames"]) == 2
    assert len(p["frames"][0]) == p["h"] and len(p["frames"][0][0]) == p["w"]
    assert p["frames"][0][0][0] == 1
    assert p["frames"][1][0][0] == 0


def test_origin_and_baseline():
    a = blank()
    b = blank()
    fill(a, 10, 21, 20, 30, RED)
    fill(b, 10, 21, 22, 35, RED)
    p = pack_animation([a, b])
    assert p["origin_x"] == 16 - 10
    assert p["baseline"] == 36 - 20
    assert p["h"] == 36 - 20


def test_palette_shared():
    a = blank()
    b = blank()
    fill(a, 0, 1, 0, 0, RED)
    fill(a, 2, 3, 0, 0, GREEN)
    fill(a, 4, 5, 0, 0, BLUE)
    fill(b, 0, 1, 0, 0, RED)
    fill(b, 2, 3, 0, 0, GREEN)
    fill(b, 4, 5, 0, 0, (9, 9, 9, 255))
    fill(b, 6, 7, 0, 0, (200, 200, 0, 255))
    pal = build_palette([a, b])
    assert pal[0] is None
    assert len(pal) - 1 == 5
    assert pal[1] == (255, 0, 0)


def test_palette_reduced():
    img = blank(64, 40)
    for x in range(40):
        img.putpixel((x, 0), (x * 6, 255 - x * 6, (x * 37) % 256, 255))
    pal = build_palette([img])
    assert len(pal) - 1 <= 15
    p = pack_animation([img], palette=pal)
    for row in p["frames"][0]:
        for v in row:
            assert 0 <= v <= 15


def test_alpha_cutoff():
    a = blank()
    a.putpixel((3, 3), (255, 0, 0, 127))
    a.putpixel((4, 3), (255, 0, 0, 128))
    p = pack_animation([a])
    assert p["w"] == 1


def _pair(recolor, move=False):
    a = blank()
    fill(a, 4, 9, 4, 9, RED)
    fill(a, 6, 7, 6, 7, GREEN)
    b = blank()
    c1, c2 = ((0, 0, 255, 255), (255, 255, 0, 255)) if recolor else (RED, GREEN)
    fill(b, 4, 9, 4, 9, c1)
    fill(b, 6, 7, 6, 7, c2)
    if move:
        b.putpixel((9, 9), CLEAR)
        b.putpixel((10, 9), c1)
    return a, b


def _anims(img):
    pal = build_palette([img])
    return {"Idle": pack_animation([img], palette=pal)}


def test_shiny_palette_only():
    a, b = _pair(True)
    assert same_indices(_anims(a), _anims(b)) is True
    a, b = _pair(True, move=True)
    assert same_indices(_anims(a), _anims(b)) is False


def raw_sprite(dex, shiny, skip=()):
    frames = {}
    ms = {}
    for name in gen_sprites.ANIM_NAMES:
        if name in skip:
            frames[name] = None
            ms[name] = None
            continue
        img = blank()
        fill(img, 8, 20, 10, 30, (dex, 100 + (50 if shiny else 0), 5, 255))
        frames[name] = [img]
        ms[name] = [200]
    return {"dex": dex, "shiny": shiny, "frames": frames, "ms": ms}


def test_missing_animation():
    sp = gen_sprites.build_sprite(raw_sprite(4, False, skip=("Eat",)))
    assert sp["anim"]["Eat"] is None
    assert sp["anim"]["Idle"] is not None and sp["anim"]["Walk"] is not None
    text = render_header([sp])
    assert "{0, 0, 0, 0, 0, nullptr, nullptr}" in text
    assert "PP_GACHI_PIX_0_Idle" in text
    assert "PP_GACHI_PIX_0_Eat" not in text


def test_dummy_complete():
    for dex in gen_sprites.DEX_LIST:
        for shiny in (False, True):
            sp = dummy_sprite(dex, shiny)
            for name in gen_sprites.ANIM_NAMES:
                a = sp["anim"][name]
                assert a is not None and len(a["frames"]) > 0
                assert len(a["ms"]) == len(a["frames"])
            assert len(sp["anim"]["Idle"]["frames"]) == 2
            assert len(sp["anim"]["Hop"]["frames"]) == 4
            assert len(sp["palette"]) == 16
    assert same_indices(dummy_sprite(1, False)["anim"], dummy_sprite(1, True)["anim"])


def test_shiny_shares_pixels_in_header():
    n = gen_sprites.build_sprite(raw_sprite(4, False))
    s = gen_sprites.build_sprite(raw_sprite(4, True))
    gen_sprites.link_shiny(n, s)
    assert s["shares"] is True
    text = render_header([n, s])
    assert text.count("PP_GACHI_PIX_0_Idle[] =") == 1
    assert "PP_GACHI_PIX_1_Idle[] =" not in text
    assert "PP_GACHI_PAL_1[] =" in text
    s2 = gen_sprites.build_sprite(raw_sprite(5, True))
    n2 = gen_sprites.build_sprite(raw_sprite(5, False))
    n2["anim"]["Idle"]["frames"][0][0][0] ^= 1
    gen_sprites.link_shiny(n2, s2)
    assert s2["shares"] is False


def test_header_shape():
    sprites = [dummy_sprite(1, False), dummy_sprite(25, True)]
    text = render_header(sprites, dummy=True)
    assert text.startswith("//")
    assert "DUMMY" in text.splitlines()[0]
    assert "PP_GACHI_SPRITE_TABLE[]" in text
    assert "PP_GACHI_SPRITE_TABLE_COUNT = 2" in text
    assert "PP_GACHI_MS_0_Hop[]" in text
    assert "PP_GACHI_PIX_0_Walk[]" in text
    assert chr(0x2014) not in text


def png_bytes(img):
    import io
    buf = io.BytesIO()
    img.save(buf, "PNG")
    return buf.getvalue()


def fake_fetch_factory(missing=()):
    calls = []

    def fetch(url):
        calls.append(url)
        for m in missing:
            if m in url:
                raise OSError("404")
        if url.endswith("AnimData.xml"):
            return XML_FULL.encode()
        sheet = Image.new("RGBA", (64, 8 * 40), CLEAR)
        for row in range(8):
            fill(sheet, 10, 20, row * 40 + 10, row * 40 + 30, RED)
            fill(sheet, 42, 52, row * 40 + 10, row * 40 + 30, RED)
        return png_bytes(sheet)

    return fetch, calls


XML_FULL = """<AnimData><Anims>
<Anim><Name>Idle</Name><FrameWidth>32</FrameWidth><FrameHeight>40</FrameHeight><Durations><Duration>12</Duration><Duration>12</Duration></Durations></Anim>
<Anim><Name>Hop</Name><FrameWidth>32</FrameWidth><FrameHeight>40</FrameHeight><Durations><Duration>6</Duration><Duration>6</Duration></Durations></Anim>
<Anim><Name>Sleep</Name><FrameWidth>32</FrameWidth><FrameHeight>40</FrameHeight><Durations><Duration>6</Duration><Duration>6</Duration></Durations></Anim>
<Anim><Name>Walk</Name><FrameWidth>32</FrameWidth><FrameHeight>40</FrameHeight><Durations><Duration>6</Duration><Duration>6</Duration></Durations></Anim>
</Anims></AnimData>"""


def test_collect_missing_sheet_and_eat():
    fetch, calls = fake_fetch_factory(missing=("Hop-Anim",))
    with tempfile.TemporaryDirectory() as d:
        raw = gen_sprites.load_raw_sprite(4, False, d, fetch)
        assert raw is not None
        assert raw["frames"]["Hop"] is None      # download failed
        assert raw["frames"]["Eat"] is None      # not in AnimData
        assert len(raw["frames"]["Idle"]) == 2
        assert raw["ms"]["Idle"] == [200, 200]
        assert any("/0004/Idle-Anim.png" in u for u in calls)
        n = len(calls)
        gen_sprites.load_raw_sprite(4, False, d, fetch)   # second run: cache hit for what exists
        assert len(calls) < 2 * n
        assert os.path.exists(os.path.join(d, "pmd", "0004", "AnimData.xml"))
        sraw = gen_sprites.load_raw_sprite(4, True, d, fetch)
        assert any("/0004/0000/0001/Idle-Anim.png" in u for u in calls)
        assert os.path.exists(os.path.join(d, "pmd", "0004", "shiny", "Idle-Anim.png"))
        assert sraw is not None


def test_collect_no_idle_and_failure():
    def failing(url):
        raise OSError("no network")

    with tempfile.TemporaryDirectory() as d:
        sprites = gen_sprites.collect_sprites(d, fetch=failing)
    assert sprites == []


def test_collect_builds_sprites():
    fetch, _ = fake_fetch_factory()
    with tempfile.TemporaryDirectory() as d:
        sprites = gen_sprites.collect_sprites(d, fetch=fetch)
    assert len(sprites) == 2 * len(gen_sprites.DEX_LIST)
    assert sprites[1]["shares"] is True   # same shapes in this fake set
    assert sprites[0]["anim"]["Eat"] is None


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    print("gen_sprites: all passed")
