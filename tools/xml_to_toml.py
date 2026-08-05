#!/usr/bin/env python3
"""One-time bootstrap: convert seqopcodes XML files to a unified opcodes.toml.

After this is run once, opcodes.toml becomes the canonical source and
toml_to_xml.py regenerates the XML on demand.

Usage:
  python3 tools/xml_to_toml.py conf/zoneopcodes.xml conf/worldopcodes.xml > conf/opcodes.toml
"""

from __future__ import annotations

import sys
import xml.etree.ElementTree as ET
from pathlib import Path


def date_iso(updated: str | None) -> str | None:
    """Convert MM/DD/YY → YYYY-MM-DD; pass through anything else unchanged."""
    if not updated:
        return None
    parts = updated.split("/")
    if len(parts) == 3 and all(p.isdigit() for p in parts):
        mm, dd, yy = parts
        # Heuristic: YY ≥ 70 → 19YY, else 20YY (matches existing data).
        year = int(yy)
        year = 1900 + year if year >= 70 else 2000 + year
        return f"{year:04d}-{int(mm):02d}-{int(dd):02d}"
    return updated


def toml_str(s: str) -> str:
    """Emit a TOML basic-string literal. Escape ``\\`` / ``"`` and control chars."""
    out = ['"']
    for ch in s:
        if ch == "\\":
            out.append("\\\\")
        elif ch == '"':
            out.append('\\"')
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\r":
            out.append("\\r")
        elif ch == "\t":
            out.append("\\t")
        elif ord(ch) < 0x20:
            out.append(f"\\u{ord(ch):04X}")
        else:
            out.append(ch)
    out.append('"')
    return "".join(out)


def parse_xml(path: Path) -> list[dict]:
    """Return list of opcode dicts from an opcodes XML file."""
    text = path.read_text()
    # The DTD reference (<!DOCTYPE seqopcodes SYSTEM "seqopcodes.dtd">) makes
    # ElementTree try to fetch the DTD. Strip it — we don't validate.
    text = text.replace('<!DOCTYPE seqopcodes SYSTEM "seqopcodes.dtd">', "")
    root = ET.fromstring(text)

    out: list[dict] = []
    for opcode in root.findall("opcode"):
        entry: dict = {
            "id":   opcode.get("id", "ffff"),
            "name": opcode.get("name", ""),
        }
        # priority/priorityNote is the patch-day hunt ladder. toml_to_xml.py
        # emits it, so dropping it here made the round-trip lossy in one
        # direction: regenerating the TOML from an XML would silently erase the
        # ordering the next rotation is hunted by.
        if (pri := opcode.get("priority")) is not None:
            entry["priority"] = int(pri)
        if (upd := opcode.get("updated")):
            entry["updated"] = date_iso(upd)
        if (impl := opcode.get("implicitlen")):
            entry["implicitlen"] = impl

        note_el = opcode.find("priorityNote")
        if note_el is not None and note_el.text and note_el.text.strip():
            entry["priority_note"] = note_el.text.strip()

        comment_el = opcode.find("comment")
        if comment_el is not None and comment_el.text and comment_el.text.strip():
            entry["comment"] = comment_el.text.strip()

        payloads = []
        for p in opcode.findall("payload"):
            payloads.append({
                "dir":           p.get("dir", "both"),
                "typename":      p.get("typename", "uint8_t"),
                "sizechecktype": p.get("sizechecktype", "none"),
            })
        if payloads:
            entry["payloads"] = payloads
        out.append(entry)
    return out


def emit_section(section_name: str, opcodes: list[dict], lines: list[str]) -> None:
    for op in opcodes:
        lines.append(f"[[{section_name}]]")
        lines.append(f"id      = {toml_str(op['id'])}")
        lines.append(f"name    = {toml_str(op['name'])}")
        if "priority" in op:
            lines.append(f"priority = {op['priority']}")
        if "priority_note" in op:
            lines.append(f"priority_note = {toml_str(op['priority_note'])}")
        if "updated" in op:
            lines.append(f"updated = {toml_str(op['updated'])}")
        if "implicitlen" in op:
            lines.append(f"implicitlen = {toml_str(op['implicitlen'])}")
        if "comment" in op:
            lines.append(f"comment = {toml_str(op['comment'])}")
        for p in op.get("payloads", []):
            lines.append("")
            lines.append(f"  [[{section_name}.payloads]]")
            lines.append(f"  dir           = {toml_str(p['dir'])}")
            lines.append(f"  typename      = {toml_str(p['typename'])}")
            lines.append(f"  sizechecktype = {toml_str(p['sizechecktype'])}")
        lines.append("")


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print("usage: xml_to_toml.py zoneopcodes.xml worldopcodes.xml [> opcodes.toml]", file=sys.stderr)
        return 2

    zone_path  = Path(argv[1])
    world_path = Path(argv[2])
    if not zone_path.exists() or not world_path.exists():
        print(f"error: missing input file ({zone_path} or {world_path})", file=sys.stderr)
        return 1

    zone_ops  = parse_xml(zone_path)
    world_ops = parse_xml(world_path)

    lines: list[str] = []
    lines.append("# Canonical source for showeq-daemon opcodes.")
    lines.append("# XML in conf/zoneopcodes.xml and conf/worldopcodes.xml is regenerated")
    lines.append("# from this file by tools/toml_to_xml.py (run via CMake on build).")
    lines.append("")
    emit_section("zone", zone_ops, lines)
    emit_section("world", world_ops, lines)

    sys.stdout.write("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
