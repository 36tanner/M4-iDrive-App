"""Build the M4/S55 BoostDeck screens from the packaged BoostDeck folder.

  python3 native/tools/m4_boostdeck_art.py --source <extracted dashboard>/BoostDeck --out <dir>/BoostDeck

Changes against the packaged app:
  * background 56300: TRANSMISSION becomes OIL TEMP and ACTIVE E becomes ENGINE (the warm-up gate);
  * yellow caution steps: bars 55601-55632 and column 56401-56420, recolored from the orange steps;
  * BoostDeck.xml: an OPTIONS > Details screen (models 120-130) with Reset peaks and logging buttons.
Native files (the .so and the collector) come from the M4 build, not from this script.
"""
import argparse
import shutil
from io import BytesIO
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

from PIL import Image, ImageDraw

TILE = (23, 27, 34, 255)
ORANGE = (255, 79, 26)
YELLOW = (255, 196, 0)
# Titles are rebuilt from glyphs cut out of the packaged lettering, so the font matches exactly.
# Each source word: its text, the x of its first glyph, and its cap top (measured on 56300.png).
WORDS = {"BOOST": (18, 25), "TIMING": (274, 25), "OIL PRESSURE": (791, 25), "COOLANT": (791, 233)}
CAP = 19
GLYPH_FROM = {"O": ("BOOST", 1), "I": ("TIMING", 1), "L": ("COOLANT", 3), "T": ("TIMING", 0),
              "E": ("OIL PRESSURE", 5), "M": ("TIMING", 2), "P": ("OIL PRESSURE", 3),
              "N": ("TIMING", 4), "G": ("TIMING", 5)}
# (box to clear, new title, origin x, cap top)
RETITLE = (((524, 18, 712, 50), "OIL TEMP", 532, 25), ((1042, 12, 1172, 44), "ENGINE", 1052, 19))
DETAIL_LINES = 11
DETAIL_MODEL, DETAIL_LABEL = 120, 420


def glyphs(image, word, x0, top):
    """Split a title into glyph images on empty columns; spaces are skipped."""
    band = image.crop((x0 - 2, top - 3, x0 + 240, top + CAP + 3))
    # A high ink threshold separates touching letters; each crop then takes one more column on either
    # side when that column is free, to keep the anti-aliased edge.
    ink = [any(band.getpixel((x, y))[0] > 90 for y in range(band.height)) for x in range(band.width)]
    spans, start = [], None
    for x, on in enumerate(ink + [False]):
        if on and start is None:
            start = x
        elif not on and start is not None:
            spans.append((start, x))
            start = None
    letters = word.replace(" ", "")
    if len(spans) < len(letters):
        raise SystemExit(f"could not split {word}: {len(spans)} glyphs")
    gaps = [b[0] - a[1] for a, b in zip(spans, spans[1:])]
    crops = []
    for i, (a, b) in enumerate(spans[:len(letters)]):
        left = a - 1 if a > 0 and (i == 0 or spans[i - 1][1] < a - 1) else a
        right = b + 1 if i + 1 >= len(spans) or spans[i + 1][0] > b + 1 else b
        crops.append(band.crop((left, 0, right, band.height)))
    return crops, gaps


def retitle(png):
    image = Image.open(BytesIO(png)).convert("RGBA")
    cut, gaps = {}, {}
    for word, (x0, top) in WORDS.items():
        cut[word], gaps[word] = glyphs(image, word, x0, top)
    # Crops carry one edge column per side, so the measured gaps shrink by two when placing them.
    letter_gap = max(0, sorted(gaps["TIMING"])[len(gaps["TIMING"]) // 2] - 2)
    space = max(gaps["OIL PRESSURE"]) - 2
    draw = ImageDraw.Draw(image)
    for box, text, x, top in RETITLE:
        draw.rectangle(box, fill=TILE)
        for ch in text:
            if ch == " ":
                x += space
                continue
            word, index = GLYPH_FROM[ch]
            glyph = cut[word][index]
            image.alpha_composite(glyph, (x, top - 3))
            x += glyph.width + letter_gap
    out = BytesIO()
    image.save(out, format="PNG", optimize=True)
    return out.getvalue()


def recolor(png, base, target):
    """Move every pixel between the empty-track color and the orange fill onto yellow."""
    image = Image.open(BytesIO(png)).convert("RGBA")
    pixels = image.load()
    for y in range(image.height):
        for x in range(image.width):
            r, g, b, a = pixels[x, y]
            amount = (r - base[0]) / float(ORANGE[0] - base[0]) if ORANGE[0] != base[0] else 0
            if amount <= 0.02:
                continue
            amount = min(1.0, amount)
            pixels[x, y] = tuple(int(round(base[i] + (target[i] - base[i]) * amount)) for i in range(3)) + (a,)
    out = BytesIO()
    image.save(out, format="PNG", optimize=True)
    return out.getvalue()


def build_images(source_zip, out_zip):
    with ZipFile(source_zip) as src:
        names = {info.filename: src.read(info.filename) for info in src.infolist()}
    bar_track = Image.open(BytesIO(names["55000.png"])).convert("RGBA").getpixel((120, 6))[:3]
    column_track = Image.open(BytesIO(names["56100.png"])).convert("RGBA").getpixel((20, 125))[:3]
    names["56300.png"] = retitle(names["56300.png"])
    for step in range(1, 33):
        names[f"{55600 + step}.png"] = recolor(names[f"{55000 + step}.png"], bar_track, YELLOW)
    for step in range(1, 21):
        names[f"{56400 + step}.png"] = recolor(names[f"{56100 + step}.png"], column_track, YELLOW)
    with ZipFile(out_zip, "w", compression=ZIP_DEFLATED) as archive:
        for name in sorted(names, key=lambda n: int(n.split(".")[0])):
            archive.writestr(ZipInfo(name), names[name], ZIP_DEFLATED)


def build_xml(source_xml, out_xml):
    tree = ET.parse(source_xml)
    app = tree.getroot().find("pluginApp")
    actions, models, states = app.find("actions"), app.find("models"), app.find("hmiStates")
    ET.SubElement(actions, "hmiAction", id="32", target="106")
    for identifier in (60, 61, 62):
        ET.SubElement(actions, "raAction", id=str(identifier))
    for line in range(DETAIL_LINES):
        ET.SubElement(models, "raDataModel", id=str(DETAIL_MODEL + line), modelType="Richtext")
    gauges = states.find("hmiState[@id='100']")
    options = gauges.find("optionComponents")
    details_button = ET.Element("button", id="34", action="32", text="Details")
    options.insert(0, details_button)
    details = ET.SubElement(states, "hmiState", id="106", parentId="100", text="Details")
    content = ET.SubElement(details, "components")
    for line in range(DETAIL_LINES):
        ET.SubElement(content, "label", id=str(DETAIL_LABEL + line), model=str(DETAIL_MODEL + line))
    for identifier, action, text in ((431, 60, "Reset peaks"), (432, 61, "Logging on"), (433, 62, "Logging off")):
        ET.SubElement(content, "button", id=str(identifier), action=str(action), text=text)
    ET.indent(tree, space="  ")
    tree.write(out_xml, encoding="utf-8", xml_declaration=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, required=True, help="packaged BoostDeck folder")
    parser.add_argument("--out", type=Path, required=True, help="output BoostDeck folder")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    build_images(args.source / "BoostDeck_ALL_Images.zip", args.out / "BoostDeck_ALL_Images.zip")
    build_xml(args.source / "BoostDeck.xml", args.out / "BoostDeck.xml")
    shutil.copy2(args.source / "BoostDeck.manifest", args.out / "BoostDeck.manifest")
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
