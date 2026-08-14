#!/usr/bin/env python3
"""One-shot converter: seqpreferences XML -> the TOML preferences format.

Used to produce `conf/seqdef.toml` from the legacy `conf/seqdef.xml`, and
available for anyone with an old user preferences file. The daemon migrates
user files itself at load; this exists for the DEFAULTS file, which is
committed and therefore converted once, by hand, on purpose.

    tools/prefs_xml_to_toml.py conf/seqdef.xml conf/seqdef.toml

Type mapping (see src/tomlpreferences.cpp for the reading half):
    <string value="x"/>  -> "x"
    <int value="5"/>     -> 5
    <bool value="true"/> -> true
    <color value="#f00"/>-> "#f00"   (SeqColor round-trips through its name())
    <font value="..."/>  -> "..."    (headless daemon; kept as an opaque string)
    <key value="..."/>   -> "..."    (ditto)

Comments become TOML comments above the key, so `seqdef.toml` stays as
self-documenting as the XML it replaces — that documentation is most of what
made the XML file worth reading.
"""

import sys
import xml.etree.ElementTree as ET

# Value element -> whether it is written bare (TOML scalar) or quoted.
SCALAR = {"int", "bool"}
STRINGY = {"string", "color", "font", "key"}


def esc(s: str) -> str:
    return s.replace("\\", "\\\\").replace('"', '\\"')


def extract(el) -> str | None:
    """The raw text of a value element.

    NOT every element uses `value=`; assuming so silently emptied all 144
    colors on the first pass, which the tier-2 goldens caught as a category
    color reading black. The real attribute per element:
        string/int/bool  value=
        color            name="gray"  OR  red=/green=/blue=
        font             family= (+ pointsize/bold/... — headless, kept whole)
        key              sequence=
    """
    if el.tag == "color":
        if "name" in el.attrib:
            return el.get("name", "")
        if "red" in el.attrib or "blue" in el.attrib:
            try:
                r = int(el.get("red", "0"))
                g = int(el.get("green", "0"))
                b = int(el.get("blue", "0"))
            except ValueError:
                return None
            return f"#{r:02x}{g:02x}{b:02x}"
        return None
    if el.tag == "font":
        # Nothing headless reads fonts; keep the whole spec so it round-trips
        # rather than inventing a lossy encoding for a dead preference.
        bits = [f"{k}={v}" for k, v in sorted(el.attrib.items())]
        return ",".join(bits)
    if el.tag == "key":
        return el.get("sequence", "")
    return el.get("value")


def render(kind: str, raw: str) -> str:
    if kind == "bool":
        # The XML uses true/false already, but be forgiving about 1/0.
        v = raw.strip().lower()
        return "true" if v in ("true", "1", "yes") else "false"
    if kind == "int":
        try:
            return str(int(raw.strip(), 0))
        except ValueError:
            # A non-numeric "int" is a bug in the source file, not something
            # to paper over with 0 — keep it as a string so it is visible.
            return f'"{esc(raw)}"'
    return f'"{esc(raw)}"'


def main() -> int:
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    root = ET.parse(src).getroot()

    out = [
        "# scryd preferences — DEFAULTS.",
        "#",
        "# Generated from seqdef.xml by tools/prefs_xml_to_toml.py. Edit this file",
        "# only if you are maintaining site-wide defaults; per-user settings belong",
        "# in the user preferences file, which the daemon writes itself.",
        "",
    ]
    kinds = set()

    for section in root.findall("section"):
        name = section.get("name", "")
        if not name:
            continue
        out.append(f"[{name}]")
        for prop in section.findall("property"):
            key = prop.get("name", "")
            if not key:
                continue
            comment = prop.findtext("comment")
            value = None
            for child in prop:
                if child.tag in SCALAR or child.tag in STRINGY:
                    kinds.add(child.tag)
                    raw = extract(child)
                    if raw is None:
                        continue
                    value = render(child.tag, raw)
                    break
            if value is None:
                continue  # comment-only property: nothing to carry over
            if comment:
                for line in comment.strip().splitlines():
                    out.append(f"# {line.strip()}")
            out.append(f"{key} = {value}")
        out.append("")

    with open(dst, "w", encoding="utf-8") as f:
        f.write("\n".join(out))
    print(f"{dst}: {len(root.findall('section'))} sections, kinds seen: {sorted(kinds)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
