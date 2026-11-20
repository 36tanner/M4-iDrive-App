"""Generate BoostDeck settings, dashboard XML, and circular dial resources."""
import argparse
from io import BytesIO
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from make_bars import COLORS, MULTICOLOR, THEMES

ROOT = Path(__file__).resolve().parents[1] / "native"
XML = ROOT / "manifests/BoostDeck.xml"
SIGNALS = re.findall(r'^SIGNAL\(\w+, "([^"]+)"',
                     (ROOT / "src/common/boost_signals.def").read_text(), re.M)
SLOTS = 9
BACKGROUND = 56300
BAR_EMPTY = 55000
VERTICAL_EMPTY = 56100
ALERT_BACKGROUND = 56999
TRANSPARENT = 57000
DIAL_FIRST = 57001
DIAL_STEPS = 12
WIDTH, HEIGHT = 1172, 400
SETTINGS = (
    ("Pressure", 3), ("Speed", 2), ("Temperature", 2), ("Gauge style", 2),
    ("Alert mode", 2), ("Coolant high", 10), ("Oil temp high", 10),
    ("Oil pressure low", 6),
)


def tile(i):
    return (258 * (i // 2), 208 * (i % 2)) if i < 8 else (1032, 0)


def placed(parent, tag, identifier, model, x, y, w, h):
    node = ET.SubElement(parent, tag, id=str(identifier), model=str(model))
    props = ET.SubElement(node, "properties")
    for prop, value in ((20, x + 24), (21, y + 13), (9, w), (10, h)):
        ET.SubElement(props, "property", id=str(prop), value=str(value))
    if tag == "image":
        for prop, value in ((2, "false"), (3, "true")):
            ET.SubElement(props, "property", id=str(prop), value=value)


def button(parent, identifier, action, text="", model=None):
    node = ET.SubElement(parent, "button", id=str(identifier), action=str(action), text=text)
    if model is not None:
        node.set("model", str(model))


def goto(actions, number, target):
    ET.SubElement(actions, "hmiAction", id=str(number), target=str(target))


def choose(actions, number, callback, return_action, target):
    group = ET.SubElement(ET.SubElement(actions, "combinedAction", id=str(number)), "actions")
    ET.SubElement(group, "raAction", id=str(callback))
    goto(group, return_action, target)


def page(states, identifier, parent, title):
    state = ET.SubElement(states, "hmiState", id=str(identifier), parentId=str(parent), text=title)
    return ET.SubElement(state, "components")


def richtext(models, identifier):
    ET.SubElement(models, "raDataModel", id=str(identifier), modelType="Richtext")


def image_model(models, identifier, initial):
    ET.SubElement(models, "imageIdModel", id=str(identifier), imageId=str(initial))


def update_xml():
    tree = ET.parse(XML)
    app = tree.find("pluginApp")
    actions, models, states = (app.find(name) for name in ("actions", "models", "hmiStates"))
    actions.clear()
    models.clear()
    states.clear()
    goto(actions, 10, 100)
    gauges = ET.SubElement(states, "hmiState", id="100", text="BoostDeck")
    opts = ET.SubElement(gauges, "optionComponents")
    goto(actions, 520, 520)
    button(opts, 33, 520, "BoostDeck settings")
    components = ET.SubElement(gauges, "components")
    image_model(models, 69, BACKGROUND)
    placed(components, "image", 209, 69, 0, 0, WIDTH, HEIGHT)
    image_model(models, 68, TRANSPARENT)
    richtext(models, 119)
    placed(components, "image", 208, 68, 0, 421, WIDTH, 43)
    placed(components, "label", 218, 119, 14, 421, WIDTH - 28, 43)

    settings = page(states, 520, 100, "BoostDeck settings")
    richtext(models, 118)
    ET.SubElement(settings, "label", id="118", model="118")
    for row, (name, count) in enumerate(SETTINGS):
        target = 521 + row
        goto(actions, target, target)
        richtext(models, 120 + row)
        button(settings, 120 + row, target, model=120 + row)
        options = page(states, target, 520, name)
        for option in range(count):
            offset = row * 20 + option
            richtext(models, 700 + offset)
            choose(actions, 2000 + offset, 2400 + offset, 2800 + offset, 520)
            button(options, 700 + offset, 2000 + offset, model=700 + offset)
    for model, target, title in ((110, 104, "Gauge color"), (360, 501, "Engine profile"),
                                  (None, 500, "Configure gauges")):
        goto(actions, target, target)
        if model is not None:
            richtext(models, model)
        button(settings, 550 + target, target, "" if model else title, model)
    colors = page(states, 104, 520, "Gauge color")
    for i, name in enumerate(THEMES):
        choose(actions, 2200 + i, 40 + i, 3000 + i, 520)
        button(colors, 390 + i, 2200 + i, name)
    profile = page(states, 501, 520, "Engine profile")
    for i, caption in enumerate(("Gasoline", "Diesel (standard OBD PIDs)")):
        choose(actions, 2210 + i, 502 + i, 3010 + i, 520)
        button(profile, 591 + i, 2210 + i, caption)
    config = page(states, 500, 520, "Configure gauges")
    for signal in range(len(SIGNALS)):
        choose(actions, 3500 + signal, 600 + signal, 4500 + signal, 500)
    for i in range(SLOTS):
        x, y = tile(i)
        for model in (300 + i, 340 + i, 370 + i):
            richtext(models, model)
        image_model(models, 320 + i, BAR_EMPTY if i < 8 else VERTICAL_EMPTY)
        image_model(models, 330 + i, TRANSPARENT)
        if i < 8:
            placed(components, "label", 260+i, 370+i, x+15, y+16, 216, 40)
            placed(components, "label", 310+i, 300+i, x+15, y+55, 216, 48)
            placed(components, "image", 280+i, 320+i, x+15, y+140, 210, 15)
            placed(components, "image", 290+i, 330+i, x+83, y+110, 64, 64)
        else:
            placed(components, "label", 260+i, 370+i, x+10, y+10, 120, 46)
            placed(components, "image", 280+i, 320+i, x+50, y+58, 40, 250)
            placed(components, "image", 290+i, 330+i, x+38, y+143, 64, 64)
            placed(components, "label", 310+i, 300+i, x+24, y+322, 112, 63)
        target = 510 + i
        goto(actions, target, target)
        button(config, 540 + i, target, model=340+i)
        options = page(states, target, 500, f"Gauge {i+1}")
        for j, caption in enumerate(SIGNALS):
            button(options, 3500 + i*40+j, 3500+j, caption)
    ET.indent(tree, space="  ")
    tree.write(XML, encoding="utf-8", xml_declaration=True)


def update_images(source, dest):
    from PIL import Image, ImageDraw
    with ZipFile(source) as original, ZipFile(dest, "w", ZIP_DEFLATED) as updated:
        background_name = f"{BACKGROUND}.png"
        background = Image.open(BytesIO(original.read(background_name))).convert("RGBA")
        if background.size != (WIDTH, HEIGHT):
            raise ValueError(f"Unexpected BoostDeck background size: {background.size}")
        draw = ImageDraw.Draw(background)
        for i in range(SLOTS):
            x, y = tile(i)
            right = 232 if i < 8 else 132
            draw.rectangle((x+8, y+13, x+right, y+51), fill=background.getpixel((x+24,y+65)))
        bitmap = BytesIO()
        background.save(bitmap, format="PNG", optimize=True)
        for entry in original.infolist():
            if entry.filename.removesuffix(".png").isdigit() and int(entry.filename[:-4]) >= ALERT_BACKGROUND:
                continue
            updated.writestr(entry, bitmap.getvalue() if entry.filename == background_name else original.read(entry))

        def write(identifier, image):
            bitmap = BytesIO()
            image.save(bitmap, format="PNG", optimize=True)
            updated.writestr(ZipInfo(f"{identifier}.png"), bitmap.getvalue(), ZIP_DEFLATED)

        warning = Image.new("RGBA", (WIDTH, 43))
        ImageDraw.Draw(warning).rounded_rectangle((0, 0, WIDTH-1, 42), radius=8, fill="#74252a")
        write(ALERT_BACKGROUND, warning)
        write(TRANSPARENT, Image.new("RGBA", (1, 1)))

        face = Image.new("RGBA", (64, 64))
        draw = ImageDraw.Draw(face)
        draw.ellipse((2,2,61,61), fill="#18212b", outline="#354656", width=1)
        draw.arc((6,6,57,57), 135, 405, fill="#596c80", width=3)

        def point(angle, radius):
            radians = math.radians(angle)
            return 32 + radius * math.cos(radians), 32 + radius * math.sin(radians)

        for tick in range(5):
            angle = 135 + 270*tick/4
            draw.line((point(angle, 20), point(angle, 24)), fill="#c2ced9", width=1)
        write(DIAL_FIRST, face)
        for palette, color in enumerate(tuple(fill for _, fill in COLORS) + MULTICOLOR):
            for step in range(DIAL_STEPS + 1):
                image = face.copy()
                draw = ImageDraw.Draw(image)
                angle = 135 + 270*step/DIAL_STEPS
                if step:
                    draw.arc((6,6,57,57), 135, angle, fill=color, width=3)
                draw.line(((32,32), point(angle,22)), fill=color, width=2)
                draw.ellipse((29,29,35,35), fill=color)
                identifier = DIAL_FIRST + 1 + palette*(DIAL_STEPS+1) + step
                write(identifier, image)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--images", type=Path)
    parser.add_argument("--images-out", type=Path)
    args = parser.parse_args()
    if bool(args.images) != bool(args.images_out) or (args.images and args.images == args.images_out):
        parser.error("provide distinct --images and --images-out paths")
    update_xml()
    if args.images:
        update_images(args.images, args.images_out)
        if args.images_out.stat().st_size>850_000:
            args.images_out.unlink()
            raise ValueError("Keep the BoostDeck image archive below 850 kB")
