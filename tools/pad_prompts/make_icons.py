#!/usr/bin/env python3
"""Converts the pad prompt icons we use from Kenney's "Input Prompts" pack (CC0) into
data/ui/prompts/<style>/<name>.dds: each icon's Default (64 px) PNG cut to its visible pixels and
written as an uncompressed A8R8G8B8 DDS, the format the game's own code reads back
(src/mods/icon_atlas.h DecodeDds). The app scales them into the game's prompt images at start
(src/mods/pad_prompts.h).

    python3 tools/pad_prompts/make_icons.py KENNEY_FOLDER [--out data/ui/prompts]

KENNEY_FOLDER is the extracted pack (the folder holding "PlayStation Series" and
"Nintendo Switch"). Needs Pillow, and PyGObject's GdkPixbuf (with its SVG loader) for the
controller pictures.
"""

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image

# Our file name -> the pack's file, per style. Nintendo's face buttons are named by their letter;
# which game prompt gets which letter is the app's choice (pad_prompts.cpp).
ICONS = {
    "playstation": {
        "cross": "PlayStation Series/Default/playstation_button_color_cross.png",
        "circle": "PlayStation Series/Default/playstation_button_color_circle.png",
        "square": "PlayStation Series/Default/playstation_button_color_square.png",
        "triangle": "PlayStation Series/Default/playstation_button_color_triangle.png",
        "l1": "PlayStation Series/Default/playstation_trigger_l1.png",
        "r1": "PlayStation Series/Default/playstation_trigger_r1.png",
        "l2": "PlayStation Series/Default/playstation_trigger_l2.png",
        "r2": "PlayStation Series/Default/playstation_trigger_r2.png",
        # Start and back as each model draws them: the PS3's triangle and bar, the PS4's and PS5's
        # tall pills (the PS4's word above them left out).
        "start_ps3": "PlayStation Series/Default/playstation3_button_start.png",
        "back_ps3": "PlayStation Series/Default/playstation3_button_select.png",
        "start_ps4": "PlayStation Series/Default/playstation4_button_options.png",
        "back_ps4": "PlayStation Series/Default/playstation4_button_share.png",
        "start_ps5": "PlayStation Series/Default/playstation5_button_options.png",
        "back_ps5": "PlayStation Series/Default/playstation5_button_create.png",
        "controller_ps3": "PlayStation Series/Vector/controller_playstation3.svg",
        "controller_ps4": "PlayStation Series/Vector/controller_playstation4.svg",
        "controller_ps5": "PlayStation Series/Vector/controller_playstation5.svg",
    },
    "nintendo": {
        "a": "Nintendo Switch/Default/switch_button_a.png",
        "b": "Nintendo Switch/Default/switch_button_b.png",
        "x": "Nintendo Switch/Default/switch_button_x.png",
        "y": "Nintendo Switch/Default/switch_button_y.png",
        "l": "Nintendo Switch/Default/switch_button_l.png",
        "r": "Nintendo Switch/Default/switch_button_r.png",
        "zl": "Nintendo Switch/Default/switch_button_zl.png",
        "zr": "Nintendo Switch/Default/switch_button_zr.png",
        "plus": "Nintendo Switch/Default/switch_button_plus.png",
        "minus": "Nintendo Switch/Default/switch_button_minus.png",
        "controller": "Nintendo Switch/Vector/controller_switch_pro.svg",
    },
}

# The sticks and the d-pad, the same names in both styles' folders. Their places in the game are up
# to 67 px wide, so they come from the pack's Double (128 px) PNGs.
STICKS = ["stick_l", "stick_r", "stick_l_horizontal", "stick_r_horizontal", "stick_l_vertical",
          "stick_r_vertical", "stick_r_press", "dpad_vertical", "dpad_horizontal", "dpad_up", "dpad_down",
          "dpad_left", "dpad_right"]
for style, folder, prefix in (("playstation", "PlayStation Series", "playstation"),
                              ("nintendo", "Nintendo Switch", "switch")):
    for name in STICKS:
        ICONS[style][name] = f"{folder}/Double/{prefix}_{name}.png"

# Icons whose word above the shape is left out: only the lowest band of visible rows is kept.
SHAPE_ONLY = {("playstation", name) for name in ("start_ps3", "back_ps3", "start_ps4", "back_ps4")}


# The help screen draws its controller picture about 350 px wide; the pack's PNGs are 128 px at
# most, so the controllers come from its SVGs, rasterized this large (the app only scales down).
SVG_SIZE = 512


def open_icon(path: Path) -> Image.Image:
    if path.suffix != ".svg":
        return Image.open(path).convert("RGBA")
    import gi

    gi.require_version("GdkPixbuf", "2.0")
    from gi.repository import GdkPixbuf

    pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_scale(str(path), SVG_SIZE, SVG_SIZE, True)
    mode = "RGBA" if pixbuf.get_has_alpha() else "RGB"
    size = (pixbuf.get_width(), pixbuf.get_height())
    return Image.frombytes(mode, size, pixbuf.get_pixels(), "raw", mode, pixbuf.get_rowstride()).convert("RGBA")


def lowest_band(image: Image.Image) -> Image.Image:
    alpha = image.getchannel("A")
    rows = [y for y in range(image.height) if alpha.crop((0, y, image.width, y + 1)).getbbox()]
    top = rows[-1]
    while top - 1 in rows:
        top -= 1
    return image.crop((0, top, image.width, rows[-1] + 1))


def dds_argb(image: Image.Image) -> bytes:
    w, h = image.size
    header = bytearray(128)
    header[0:4] = b"DDS "
    struct.pack_into("<7I", header, 4, 124, 0x100F, h, w, w * 4, 0, 0)
    struct.pack_into("<8I", header, 76, 32, 0x41, 0, 32, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000)
    struct.pack_into("<I", header, 108, 0x1000)
    r, g, b, a = image.split()
    pixels = Image.merge("RGBA", (b, g, r, a)).tobytes()  # B, G, R, A in memory
    return bytes(header) + pixels


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("kenney")
    parser.add_argument("--out", default=str(Path(__file__).resolve().parents[2] / "data" / "ui" / "prompts"))
    args = parser.parse_args()
    root, out = Path(args.kenney), Path(args.out)
    for style, icons in ICONS.items():
        (out / style).mkdir(parents=True, exist_ok=True)
        for name, source in icons.items():
            image = open_icon(root / source)
            if (style, name) in SHAPE_ONLY:
                image = lowest_band(image)
            image = image.crop(image.getchannel("A").getbbox())
            (out / style / f"{name}.dds").write_bytes(dds_argb(image))
            print(f"{style}/{name}.dds {image.size[0]}x{image.size[1]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
