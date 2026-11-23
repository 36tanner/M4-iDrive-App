"""Assemble the F82 M4 / S55 dashboard package.

  python3 native/tools/m4_package.py --dashboard packages/dashboard.zip --native native/out --out m4-dashboard.zip

--native is the build.sh output (M4 build: see docs/M4_S55.md). Without it, --resources-only writes the
screens alone for review; that ZIP cannot be installed because its native files would be the stock ones.

The package holds BoostDeck (S55 profile, warm-up column, caution colors, Details, logging), ShiftDeck
(Performance timers) and the stock Engine Fault Codes. MHD+ Suite is left out: S55 map switching needs
its own checks.
"""
import argparse
import hashlib
import sys
import tempfile
from pathlib import Path
from xml.etree import ElementTree as ET
from zipfile import ZIP_DEFLATED, ZipFile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import m4_boostdeck_art  # noqa: E402

TIMER_LINES, TIMER_MODEL, TIMER_LABEL = 7, 80, 500
NATIVE = {"BoostDeck": ("libBoostDeck.so", "boostdeck-collector-qnx"),
          "ShiftDeck": ("libShiftDeck.so", "shift-deck-qnx")}


def shiftdeck_xml(source, out):
    tree = ET.parse(source)
    app = tree.getroot().find("pluginApp")
    actions, models, states = app.find("actions"), app.find("models"), app.find("hmiStates")
    ET.SubElement(actions, "hmiAction", id="48", target="102")
    ET.SubElement(actions, "raAction", id="49")
    for line in range(TIMER_LINES):
        ET.SubElement(models, "raDataModel", id=str(TIMER_MODEL + line), modelType="Richtext")
    options = states.find("hmiState[@id='100']").find("optionComponents")
    options.insert(1, ET.Element("button", id="32", action="48", text="Performance timers"))
    timers = ET.SubElement(states, "hmiState", id="102", parentId="100", text="Performance timers")
    content = ET.SubElement(timers, "components")
    for line in range(TIMER_LINES):
        ET.SubElement(content, "label", id=str(TIMER_LABEL + line), model=str(TIMER_MODEL + line))
    ET.SubElement(content, "button", id=str(TIMER_LABEL + TIMER_LINES), action="49", text="Reset timers")
    ET.indent(tree, space="  ")
    tree.write(out, encoding="utf-8", xml_declaration=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--dashboard", type=Path, required=True, help="packages/dashboard.zip")
    parser.add_argument("--native", type=Path, help="build.sh output directory from the M4 build")
    parser.add_argument("--resources-only", action="store_true", help="screens only, for review")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    if not args.native and not args.resources_only:
        parser.error("--native is required unless --resources-only")

    with tempfile.TemporaryDirectory() as tmp:
        stock, build = Path(tmp) / "stock", Path(tmp) / "build"
        with ZipFile(args.dashboard) as z:
            z.extractall(stock)
        for app in ("BoostDeck", "ShiftDeck", "EngineCodes"):
            (build / app).mkdir(parents=True)
            for f in (stock / app).iterdir():
                (build / app / f.name).write_bytes(f.read_bytes())
        m4_boostdeck_art.build_images(stock / "BoostDeck/BoostDeck_ALL_Images.zip",
                                      build / "BoostDeck/BoostDeck_ALL_Images.zip")
        m4_boostdeck_art.build_xml(stock / "BoostDeck/BoostDeck.xml", build / "BoostDeck/BoostDeck.xml")
        shiftdeck_xml(stock / "ShiftDeck/ShiftDeck.xml", build / "ShiftDeck/ShiftDeck.xml")
        if args.native:
            for app, names in NATIVE.items():
                for name in names:
                    source = args.native / name
                    if not source.is_file():
                        raise SystemExit(f"Missing native output: {source}")
                    (build / app / name).write_bytes(source.read_bytes())
        lines = []
        for f in sorted(p for p in build.rglob("*") if p.is_file()):
            lines.append(f"{hashlib.sha256(f.read_bytes()).hexdigest()}  {f.relative_to(build).as_posix()}")
        notes = ("F82 M4 / S55 dashboard: BoostDeck, ShiftDeck and Engine Fault Codes.\n"
                 "See docs/M4_S55.md in the repository for install, the Details screen and logging.\n")
        if not args.native:
            notes = "REVIEW ONLY: native files are the stock builds. Do not install.\n" + notes
        with ZipFile(args.out, "w", compression=ZIP_DEFLATED) as out:
            for f in sorted(p for p in build.rglob("*") if p.is_file()):
                out.write(f, f.relative_to(build).as_posix())
            out.writestr("SHA256SUMS", "\n".join(lines) + "\n")
            out.writestr("README.md", notes)
    print(f"Wrote {args.out}")


if __name__ == "__main__":
    main()
