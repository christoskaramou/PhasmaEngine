"""Rebuild PRISM's deterministic, original procedural artwork with Pillow and NumPy."""

from pathlib import Path
import random

import numpy as np
from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1] / "Assets" / "Textures"
SIZE = (1600, 1000)
SCALE = 2
COLORS = {
    "I": (72, 225, 243),
    "O": (255, 215, 107),
    "T": (184, 132, 255),
    "S": (97, 239, 180),
    "Z": (255, 112, 153),
    "J": (112, 159, 255),
    "L": (255, 170, 103),
}


def font(size, bold=False):
    name = "segoeuib.ttf" if bold else "segoeui.ttf"
    return ImageFont.truetype(str(Path("C:/Windows/Fonts") / name), round(size * SCALE))


def text(draw, xy, content, size=14, color=(137, 160, 185, 255), bold=False, spacing=0):
    x, y = (round(v * SCALE) for v in xy)
    face = font(size, bold)

    if spacing == 0:
        draw.text((x, y), content, font=face, fill=color)
        return

    for char in content:
        draw.text((x, y), char, font=face, fill=color)
        x += draw.textlength(char, font=face) + spacing * SCALE


def rect(draw, bounds, radius, fill, outline=None, width=1):
    draw.rounded_rectangle(
        tuple(round(v * SCALE) for v in bounds),
        radius=round(radius * SCALE),
        fill=fill,
        outline=outline,
        width=round(width * SCALE),
    )


def line(draw, coords, color, width=1):
    draw.line(tuple(round(v * SCALE) for v in coords), fill=color, width=round(width * SCALE))


def atmosphere(ribbon_gain=1.0, mist_gain=1.0):
    y, x = np.mgrid[0:SIZE[1], 0:SIZE[0]].astype(np.float32)
    base = np.empty((*y.shape, 3), dtype=np.float32)
    base[:] = (5, 10, 23)

    fields = (
        (300, 370, 420, 300, (6, 24, 33)),
        (1300, 390, 410, 420, (22, 12, 44)),
        (820, 920, 600, 250, (2, 13, 19)),
        (710, 60, 720, 110, (4, 10, 20)),
    )
    for cx, cy, sx, sy, rgb in fields:
        falloff = np.exp(-(((x - cx) / sx) ** 2 + ((y - cy) / sy) ** 2))
        base += falloff[:, :, None] * np.array(rgb)

    left_curve = 90 + 125 * np.sin(y * .0035) + .00015 * (y - 420) ** 2
    right_curve = 1440 - 130 * np.sin(y * .0038)
    envelope = np.exp(-((y - 410) / 390) ** 2)
    for curve, rgb in ((left_curve, (1, 15, 17)), (right_curve, (12, 3, 23))):
        ribbon = np.exp(-((x - curve) / 65) ** 2) * envelope
        base += ribbon[:, :, None] * np.array(rgb) * ribbon_gain

    mist = np.sin(x * .007 + y * .009) * np.sin(y * .014 - x * .003)
    base += mist[:, :, None] * np.array((.6, 1.3, 2.0)) * mist_gain

    noise = np.random.default_rng(41).normal(0, .4, y.shape)
    base += noise[:, :, None]

    image = Image.fromarray(np.uint8(np.clip(base, 0, 255))).convert("RGBA")
    return image.resize((SIZE[0] * SCALE, SIZE[1] * SCALE), Image.Resampling.BICUBIC)


def sky(image):
    layer = Image.new("RGBA", image.size)
    draw = ImageDraw.Draw(layer)

    random.seed(72)
    for _ in range(170):
        x = random.randrange(70, 1540)
        y = random.randrange(35, 930)
        alpha = random.randrange(22, 95)
        radius = random.choice((.4, .5, .65, .9))
        rect(draw, (x, y, x + radius, y + radius), radius, (151, 199, 226, alpha))

    for bounds, start, end, color in (
        ((-600, 10, 940, 1230), 170, 287, (83, 190, 206, 18)),
        ((-620, 28, 960, 1250), 170, 287, (83, 190, 206, 8)),
        ((950, -330, 2210, 1060), 85, 225, (173, 133, 237, 22)),
        ((970, -350, 2230, 1080), 85, 225, (173, 133, 237, 9)),
        ((90, -500, 1520, 1210), 18, 112, (151, 184, 235, 12)),
    ):
        draw.arc(tuple(v * SCALE for v in bounds), start, end, fill=color, width=2)

    for x, y in ((207, 335), (1380, 727), (1430, 237), (157, 730)):
        line(draw, (x - 4, y, x + 4, y), (145, 211, 231, 70))
        line(draw, (x, y - 4, x, y + 4), (145, 211, 231, 70))

    image.alpha_composite(layer)


def card(draw, bounds, accent=(132, 159, 208)):
    x0, y0, x1, y1 = bounds
    rect(draw, bounds, 15, (10, 21, 39, 185), (*accent, 55))
    line(draw, (x0 + 17, y0 + 1, x1 - 17, y0 + 1), (*accent, 45))
    line(draw, (x0 + 17, y1 - 1, x1 - 17, y1 - 1), (6, 13, 26, 180))


def header(draw):
    cubes = (
        (310, 88, (103, 229, 235)),
        (329, 69, (185, 167, 255)),
        (329, 88, (130, 159, 255)),
    )
    for x, y, color in cubes:
        rect(draw, (x, y, x + 15, y + 15), 3, (*color, 238))
        line(draw, (x + 3, y + 2, x + 11, y + 2), (235, 250, 255, 190))

    text(draw, (370, 55), "PRISM", 54, (231, 242, 253, 255), True, 6)
    text(draw, (374, 122), "FIND YOUR FLOW.", 11, (110, 161, 182, 255), False, 3)
    text(draw, (815, 110), "MARATHON  /  01", 12, (159, 151, 199, 255), False, 1.5)
    line(draw, (310, 156, 1290, 156), (119, 158, 190, 39))
    line(draw, (310, 156, 395, 156), (108, 227, 231, 165))
    rect(draw, (1120, 92, 1290, 136), 22, (20, 35, 53, 140), (144, 182, 211, 75))


def panels(draw):
    card(draw, (310, 200, 550, 350), (108, 187, 200))
    card(draw, (310, 390, 550, 580))
    card(draw, (310, 610, 550, 735))
    card(draw, (1050, 200, 1290, 630), (162, 143, 217))
    card(draw, (1050, 660, 1290, 880))

    for xy, label in (((330, 220), "HOLD"), ((330, 412), "SCORE"), ((1070, 220), "UP NEXT")):
        text(draw, xy, label, 12, (147, 174, 196, 255), True, 2)

    text(draw, (330, 548), "BEST", 10, (111, 139, 162, 255), True, 1.3)
    line(draw, (330, 530, 530, 530), (111, 144, 172, 38))
    text(draw, (330, 632), "LEVEL", 11, (133, 164, 187, 255), True, 1.5)
    text(draw, (447, 632), "LINES", 11, (133, 164, 187, 255), True, 1.5)
    line(draw, (430, 635, 430, 711), (121, 149, 180, 35))
    text(draw, (330, 785), "MAKE SPACE.", 14, (140, 178, 196, 255), True, 1.3)
    text(draw, (330, 811), "One piece at a time.", 13, (90, 120, 143, 255))


def keycap(draw, x, y, label, width=29):
    rect(draw, (x, y, x + width, y + 23), 5, (30, 45, 65, 255), (139, 165, 194, 75))
    face = font(10, True)

    length = draw.textlength(label, font=face) / SCALE
    text(draw, (x + (width - length) / 2, y + 3), label, 10, (188, 207, 225, 255), True)


def controls(draw):
    text(draw, (1070, 680), "CONTROLS", 11, (136, 161, 188, 255), True, 1.4)

    for y, keys, label in (
        (711, ("←", "→"), "MOVE"),
        (741, ("↑", "X"), "ROTATE"),
        (771, ("SPACE",), "DROP"),
        (801, ("C",), "HOLD"),
        (831, ("P",), "PAUSE"),
    ):
        for index, key in enumerate(keys):
            keycap(draw, 1070 + index * 33, y, key, 62 if key == "SPACE" else 29)

        text(draw, (1151, y + 3), label, 10, (132, 155, 178, 255), False, 1.0)


def make_aurora():
    # The old 2D backdrop's sky with brighter curtains, used as the emissive texture of the 3D aurora wall.
    image = atmosphere(ribbon_gain=9, mist_gain=4)
    sky(image)

    image = image.resize(SIZE, Image.Resampling.LANCZOS).convert("RGB")
    image.save(ROOT / "aurora.png", optimize=True)


def make_hud():
    # Transparent overlay: cards, labels and controls; the board cutout stays clear for the 3D board.
    image = Image.new("RGBA", (SIZE[0] * SCALE, SIZE[1] * SCALE))
    draw = ImageDraw.Draw(image)
    header(draw)
    panels(draw)
    controls(draw)

    line(draw, (310, 922, 1290, 922), (112, 151, 183, 34))
    text(draw, (310, 943), "PHASMA ENGINE", 10, (108, 143, 167, 255), True, 2)
    text(draw, (936, 943), "A LITTLE ORDER. A LITTLE CHAOS.", 10, (107, 128, 158, 255), False, 1)

    image = image.resize(SIZE, Image.Resampling.LANCZOS)
    image.save(ROOT / "hud.png", optimize=True)


def header_versus(draw):
    cubes = (
        (250, 70, (103, 229, 235)),
        (266, 54, (185, 167, 255)),
        (266, 70, (130, 159, 255)),
    )
    for x, y, color in cubes:
        rect(draw, (x, y, x + 13, y + 13), 3, (*color, 238))
        line(draw, (x + 3, y + 2, x + 10, y + 2), (235, 250, 255, 190))

    text(draw, (296, 40), "PRISM", 44, (231, 242, 253, 255), True, 5)
    text(draw, (299, 97), "FIND YOUR FLOW.", 10, (110, 161, 182, 255), False, 3)
    text(draw, (724, 76), "YOU  VS  JEV", 14, (159, 151, 199, 255), True, 2)
    line(draw, (250, 130, 1350, 130), (119, 158, 190, 39))
    line(draw, (250, 130, 335, 130), (108, 227, 231, 165))
    rect(draw, (1180, 70, 1350, 114), 22, (20, 35, 53, 140), (144, 182, 211, 75))


def seat_cards(draw, card_x, column_x, label, accent):
    # Score card above the board, hold and next cards on the board's outer side.
    card(draw, (card_x, 150, card_x + 330, 232), accent)
    text(draw, (card_x + 16, 160), label, 11, (*accent, 255), True, 2)
    text(draw, (card_x + 196, 160), "LEVEL", 9, (133, 164, 187, 255), True, 1.3)
    text(draw, (card_x + 252, 160), "LINES", 9, (133, 164, 187, 255), True, 1.3)
    line(draw, (card_x + 186, 166, card_x + 186, 222), (121, 149, 180, 35))

    card(draw, (column_x, 250, column_x + 145, 370), accent)
    card(draw, (column_x, 390, column_x + 145, 850))
    text(draw, (column_x + 16, 262), "HOLD", 11, (147, 174, 196, 255), True, 2)
    text(draw, (column_x + 16, 402), "NEXT", 11, (147, 174, 196, 255), True, 2)


def make_hud_versus():
    # Race overlay: both board cutouts (prism_stage.lua CUTOUTS, rows 250-850) stay clear for the 3D boards.
    image = Image.new("RGBA", (SIZE[0] * SCALE, SIZE[1] * SCALE))
    draw = ImageDraw.Draw(image)
    header_versus(draw)
    seat_cards(draw, 410, 250, "YOU", (108, 187, 200))
    seat_cards(draw, 860, 1205, "JEV", (162, 143, 217))
    text(draw, (412, 860), "←  →  MOVE    ↑  X  ROTATE    SPACE  DROP    C  HOLD", 9, (110, 136, 160, 255))

    line(draw, (250, 930, 1350, 930), (112, 151, 183, 34))
    text(draw, (250, 945), "PHASMA ENGINE", 10, (108, 143, 167, 255), True, 2)
    text(draw, (1132, 945), "A LITTLE ORDER. A LITTLE CHAOS.", 10, (107, 128, 158, 255), False, 1)

    image = image.resize(SIZE, Image.Resampling.LANCZOS)
    image.save(ROOT / "hud_versus.png", optimize=True)


def make_block(kind, color):
    size = 256
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    edge = np.maximum(abs(x - 127.5), abs(y - 127.5))
    alpha = np.exp(-np.maximum(edge - 96, 0) ** 2 / 180) * 52

    rgba = np.empty((size, size, 4), dtype=np.uint8)
    rgba[:, :, :3] = color
    rgba[:, :, 3] = alpha.astype(np.uint8)
    image = Image.fromarray(rgba)

    body = Image.new("RGBA", (size, size))
    mask = Image.new("L", (size, size))
    ImageDraw.Draw(mask).rounded_rectangle((24, 24, 231, 231), radius=19, fill=255)
    light = .63 + .25 * (1 - y / size) + .16 * (1 - x / size)
    gradient = np.clip(np.array(color)[None, None, :] * light[:, :, None], 0, 255).astype(np.uint8)
    body.paste(Image.fromarray(gradient).convert("RGBA"), (0, 0), mask)

    details = Image.new("RGBA", (size, size))
    draw = ImageDraw.Draw(details)
    draw.rounded_rectangle((25, 25, 230, 230), 18, outline=(*color, 255), width=3)
    draw.polygon(((36, 39), (218, 39), (199, 58), (56, 58)), fill=(255, 255, 255, 45))
    draw.polygon(((37, 44), (56, 60), (56, 198), (37, 216)), fill=(245, 255, 255, 31))
    draw.polygon(((218, 43), (218, 215), (199, 198), (199, 60)), fill=(2, 7, 24, 42))
    draw.polygon(((39, 218), (216, 218), (197, 199), (58, 199)), fill=(2, 7, 24, 55))
    draw.rounded_rectangle((57, 57, 198, 198), 7, outline=(255, 255, 255, 30), width=2)
    draw.line((42, 33, 209, 33), fill=(241, 255, 255, 218), width=3)
    draw.line((33, 45, 33, 196), fill=(230, 255, 255, 143), width=2)
    draw.polygon(((61, 62), (139, 62), (61, 140)), fill=(255, 255, 255, 17))

    body.alpha_composite(details)
    image.alpha_composite(body)
    image = image.resize((128, 128), Image.Resampling.LANCZOS)
    image.save(ROOT / f"block_{kind}.png")


def save_gray(name, value):
    gray = np.clip(value * 255, 0, 255).astype(np.uint8)
    Image.fromarray(np.dstack([gray, gray, gray])).save(ROOT / name)


def make_crystal():
    # One neutral texture pair shared by every block cube; colour comes from per-cube material factors.
    size = 256
    y, x = np.mgrid[0:size, 0:size].astype(np.float32)
    u = (x + 0.5) / size
    v = (y + 0.5) / size
    edge = np.minimum(np.minimum(u, 1 - u), np.minimum(v, 1 - v))

    # Colour: a bright bevel band, a slightly darker table and a soft diagonal facet highlight.
    bevel = np.clip(1 - edge / 0.12, 0, 1)
    facet = np.clip(1 - np.abs(u - v) / 0.35, 0, 1) * 0.12
    save_gray("crystal.png", 0.62 + 0.38 * bevel + facet - 0.10 * v)

    # Emission: hot rims fading to a dim core, plus a faint inner diamond so faces read as cut crystal.
    rim = np.clip(1 - edge / 0.07, 0, 1) ** 1.5
    diamond = np.abs(np.abs(u - 0.5) + np.abs(v - 0.5) - 0.32)
    inner = np.clip(1 - diamond / 0.04, 0, 1) * 0.25
    # Sampled raw while the scene keeps linear_color off: faces glow at 0.6 of the material glow, rims at 1.0+.
    save_gray("crystal_glow.png", 0.6 + 0.4 * rim + inner)


def save_alpha(name, alpha):
    pixels = np.full((*alpha.shape, 4), 255, dtype=np.uint8)
    pixels[:, :, 3] = np.clip(alpha, 0, 255).astype(np.uint8)
    Image.fromarray(pixels).save(ROOT / name)


def make_effects():
    y, x = np.mgrid[0:96, 0:96].astype(np.float32)
    radius = np.sqrt((x - 47.5) ** 2 + (y - 47.5) ** 2) / 47.5
    across = np.abs(x - 47.5) / 47.5
    along = np.abs(y - 47.5) / 47.5

    save_alpha("glow.png", np.maximum(0, 1 - radius) ** 2.6 * 255)

    # A long thin diamond: particles stretched along their velocity read as crystal shards.
    shard = np.maximum(0, 1 - across / 0.28 - along)
    save_alpha("shard.png", shard ** 1.4 * 255)

    # A four-point star with a soft core for sparks.
    vertical = np.maximum(0, 1 - across / 0.08 - along)
    horizontal = np.maximum(0, 1 - along / 0.08 - across)
    core = np.maximum(0, 1 - radius / 0.35) ** 2
    save_alpha("spark.png", np.maximum(np.maximum(vertical, horizontal), core) * 255)


def check():
    aurora = Image.open(ROOT / "aurora.png")
    assert aurora.size == SIZE and aurora.mode == "RGB"

    hud = Image.open(ROOT / "hud.png")
    assert hud.size == SIZE and hud.mode == "RGBA"
    assert hud.getpixel((800, 540))[3] == 0, "the 3D board shows through the HUD's board cutout"
    assert hud.getpixel((430, 300))[3] > 0, "the HUD draws its glass cards"

    race = Image.open(ROOT / "hud_versus.png")
    assert race.size == SIZE and race.mode == "RGBA"
    for x, y in ((575, 550), (412, 252), (738, 848), (1025, 550), (862, 252), (1188, 848)):
        assert race.getpixel((x, y))[3] == 0, f"both 3D boards show through the race cutouts ({x}, {y})"
    for x, y in ((500, 200), (950, 200), (320, 300), (1280, 600)):
        assert race.getpixel((x, y))[3] > 0, f"the race overlay draws its cards ({x}, {y})"

    for kind in COLORS:
        block = Image.open(ROOT / f"block_{kind}.png")
        assert block.mode == "RGBA" and block.size == (128, 128)
        assert block.getpixel((64, 64))[3] == 255

    crystal = Image.open(ROOT / "crystal.png")
    crystal_glow = Image.open(ROOT / "crystal_glow.png")
    assert crystal.size == (256, 256) and crystal_glow.size == (256, 256)
    assert crystal_glow.getpixel((4, 128))[0] > 1.3 * crystal_glow.getpixel((128, 128))[0], "rims outshine centres"

    for name in ("glow.png", "shard.png", "spark.png"):
        effect = Image.open(ROOT / name)
        assert effect.mode == "RGBA", name
        assert effect.getpixel((48, 48))[3] > 200, name
        assert effect.getpixel((0, 0))[3] == 0, name


def main():
    ROOT.mkdir(parents=True, exist_ok=True)
    make_aurora()
    make_hud()
    make_hud_versus()
    for kind, color in COLORS.items():
        make_block(kind, color)

    make_crystal()
    make_effects()
    check()
    print(f"Rebuilt 15 PRISM textures in {ROOT}")


if __name__ == "__main__":
    main()
