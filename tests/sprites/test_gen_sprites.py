import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools", "sprites"))

from PIL import Image  # noqa: E402

import gen_sprites  # noqa: E402
from gen_sprites import pack_image, rgb565, dummy_image, render_header  # noqa: E402


def test_rgb565():
    assert rgb565(255, 255, 255) == 0xFFFF
    assert rgb565(0, 0, 0) == 0
    assert rgb565(255, 0, 0) == 0xF800
    assert rgb565(0, 255, 0) == 0x07E0
    assert rgb565(0, 0, 255) == 0x001F


def test_pack_crops():
    img = Image.new("RGBA", (10, 10), (0, 0, 0, 0))
    for x in range(4, 7):
        for y in range(5, 7):
            img.putpixel((x, y), (255, 0, 0, 255))
    w, h, _pal, _pix = pack_image(img)
    assert w == 3
    assert h == 2


def test_pack_nibbles():
    img = Image.new("RGBA", (3, 1))
    img.putpixel((0, 0), (255, 0, 0, 255))
    img.putpixel((1, 0), (0, 255, 0, 255))
    img.putpixel((2, 0), (255, 0, 0, 255))
    w, h, pal, pix = pack_image(img)
    assert (w, h) == (3, 1)
    assert pal[1] == 0xF800
    assert pal[2] == 0x07E0
    assert list(pix) == [0x12, 0x10]


def test_pack_transparent_is_zero():
    img = Image.new("RGBA", (3, 1), (0, 0, 0, 0))
    img.putpixel((0, 0), (255, 0, 0, 255))
    img.putpixel((2, 0), (255, 0, 0, 255))
    w, h, pal, pix = pack_image(img)
    assert w == 3
    assert list(pix) == [0x10, 0x10]


def test_pack_reduces_colors():
    img = Image.new("RGBA", (40, 1))
    for x in range(40):
        img.putpixel((x, 0), (x * 6, 255 - x * 6, (x * 37) % 256, 255))
    w, h, pal, pix = pack_image(img)
    assert len(pal) <= 16
    for b in pix:
        assert (b >> 4) <= 15
        assert (b & 15) <= 15


def test_pack_fully_transparent():
    img = Image.new("RGBA", (5, 5), (0, 0, 0, 0))
    w, h, pal, pix = pack_image(img)
    assert (w, h) == (1, 1)
    assert list(pix) == [0]


def test_dummy_has_no_files():
    a = dummy_image(1, False)
    b = dummy_image(25, False)
    s = dummy_image(1, True)
    assert a.size == (24, 24)
    assert b.size == (24, 24)
    assert a.tobytes() != b.tobytes()
    assert a.tobytes() != s.tobytes()


def test_header_shape():
    sprites = [
        (1, False, pack_image(dummy_image(1, False))),
        (25, True, pack_image(dummy_image(25, True))),
    ]
    text = render_header(sprites)
    assert "PP_GACHI_SPRITE_TABLE_COUNT = 2" in text
    assert "dex 1" in text or "{1," in text
    assert "25" in text


def test_download_failure_leaves_sprite_out():
    import tempfile

    def failing(url):
        raise OSError("no network")

    with tempfile.TemporaryDirectory() as d:
        sprites = gen_sprites.collect_sprites(d, fetch=failing)
    assert sprites == []


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    print("gen_sprites: all passed")
