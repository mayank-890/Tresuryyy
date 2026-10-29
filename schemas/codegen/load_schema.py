"""Minimal YAML loader for journal record schemas (stdlib only)."""

from __future__ import annotations

import re
from pathlib import Path
from typing import Any


def load_schema(path: Path) -> dict[str, Any]:
    lines = path.read_text(encoding="utf-8").splitlines()
    return _parse_mapping(lines, 0, 0)[0]


def _indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def _parse_mapping(lines: list[str], start: int, base_indent: int) -> tuple[dict[str, Any], int]:
    result: dict[str, Any] = {}
    i = start
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            i += 1
            continue
        indent = _indent(line)
        if indent < base_indent:
            break
        if indent > base_indent:
            raise ValueError(f"unexpected indent at line {i + 1}")
        if stripped.startswith("- "):
            break

        key, rest = stripped.split(":", 1)
        key = key.strip()
        rest = rest.strip()
        if rest == "":
            j = i + 1
            if j >= len(lines):
                result[key] = {}
                i = j
                continue
            child_indent = _indent(lines[j])
            if j < len(lines) and lines[j].strip().startswith("- "):
                lst, i = _parse_list(lines, i + 1, child_indent)
                result[key] = lst
                continue
            nested, i = _parse_mapping(lines, i + 1, child_indent)
            result[key] = nested
            continue

        result[key] = _scalar(rest)
        i += 1
    return result, i


def _parse_list(lines: list[str], start: int, base_indent: int) -> tuple[list[Any], int]:
    items: list[Any] = []
    i = start
    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            i += 1
            continue
        indent = _indent(line)
        if indent < base_indent:
            break
        if not stripped.startswith("- "):
            break
        content = stripped[2:].strip()
        if content.startswith("{") and content.endswith("}"):
            items.append(_parse_inline_mapping(content))
            i += 1
            continue
        if ":" in content:
            item_dict: dict[str, Any] = {}
            j = i
            item_base = indent + 2
            while j < len(lines):
                s = lines[j]
                st = s.strip()
                if not st or st.startswith("#"):
                    j += 1
                    continue
                ind = _indent(s)
                if ind < indent:
                    break
                if ind == indent:
                    if not st.startswith("- "):
                        break
                    if j != i:
                        break
                    st = st[2:].strip()
                elif ind < item_base:
                    break
                if st.startswith("- "):
                    break
                key, rest = st.split(":", 1)
                key = key.strip()
                rest = rest.strip()
                if rest == "":
                    k = j + 1
                    if k < len(lines):
                        child_indent = _indent(lines[k])
                        if lines[k].strip().startswith("- "):
                            nested_list, k = _parse_list(lines, k, child_indent)
                            item_dict[key] = nested_list
                        else:
                            nested_map, k = _parse_mapping(lines, k, child_indent)
                            item_dict[key] = nested_map
                    else:
                        item_dict[key] = {}
                    j = k
                    continue
                item_dict[key] = _scalar(rest)
                j += 1
            items.append(item_dict)
            i = j
            continue
        raise ValueError(f"unsupported list item at line {i + 1}: {stripped!r}")
    return items, i


def _scalar(value: str) -> Any:
    if value.startswith("{") and value.endswith("}"):
        return _parse_inline_mapping(value)
    if re.fullmatch(r"0x[0-9a-fA-F]+", value):
        return int(value, 16)
    if re.fullmatch(r"-?\d+", value):
        return int(value)
    if (value.startswith('"') and value.endswith('"')) or (
        value.startswith("'") and value.endswith("'")
    ):
        return value[1:-1]
    return value


def _parse_inline_mapping(text: str) -> dict[str, Any]:
    text = text.strip()
    if not text.startswith("{") or not text.endswith("}"):
        raise ValueError(f"expected inline mapping, got {text!r}")
    inner = text[1:-1].strip()
    if not inner:
        return {}
    result: dict[str, Any] = {}
    parts = re.split(r",(?=(?:[^\"']*[\"'][^\"']*[\"'])*[^\"']*$)", inner)
    for part in parts:
        part = part.strip()
        if not part:
            continue
        key, val = part.split(":", 1)
        result[key.strip()] = _scalar(val.strip())
    return result
