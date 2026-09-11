#!/usr/bin/env python3
"""Validate a descriptor and one CTLST Widget Protocol v1 frame."""

from __future__ import annotations

import argparse
import json
import re
import select
import shutil
import subprocess
from pathlib import Path


ID = re.compile(r"[a-z0-9][a-z0-9_-]*\Z")
NODE_TYPES = {"rect", "circle", "line", "text"}
THEME_TOKENS = {
    "@ctlst_bg",
    "@ctlst_panel",
    "@ctlst_panel_alt",
    "@ctlst_raised",
    "@ctlst_overlay",
    "@ctlst_text",
    "@ctlst_muted",
    "@ctlst_line",
    "@ctlst_line_soft",
    "@ctlst_accent",
    "@ctlst_accent_soft",
    "@ctlst_warm",
    "@ctlst_danger",
    "@ctlst_danger_active",
    "@ctlst_selected_text",
}
HEX_COLOR = re.compile(r"#[0-9a-fA-F]{6}(?:[0-9a-fA-F]{2})?\Z")


def parse_descriptor(path: Path) -> dict[str, str]:
    result: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or line.startswith("-"):
            continue
        if ":" not in line:
            continue
        key, value = line.split(":", 1)
        result[key.strip()] = value.strip().strip("'\"")
    return result


def check_descriptor(data: dict[str, str]) -> None:
    required = {
        "id", "name", "kind", "protocol", "interaction", "min_columns",
        "min_rows", "default_columns", "default_rows", "exec",
    }
    missing = sorted(required - data.keys())
    if missing:
        raise ValueError("missing descriptor fields: " + ", ".join(missing))
    if not ID.fullmatch(data["id"]):
        raise ValueError("invalid widget id")
    if data["id"] in {"clock", "calendar", "weather", "glance"}:
        raise ValueError("descriptor cannot replace a built-in Home widget")
    if data["kind"] != "exec" or data["protocol"] != "ctlst-widget-1":
        raise ValueError("descriptor must select exec + ctlst-widget-1")
    if data["interaction"] not in {"pointer", "launch", "none"}:
        raise ValueError("invalid interaction")
    min_columns = int(data["min_columns"])
    min_rows = int(data["min_rows"])
    columns = int(data["default_columns"])
    rows = int(data["default_rows"])
    if not 1 <= min_columns <= columns <= 5 or not 1 <= min_rows <= rows <= 4:
        raise ValueError("default span does not fit the 5x4 cluster")


def check_frame(frame: object) -> None:
    if not isinstance(frame, dict) or frame.get("ctlst_widget") != 1:
        raise ValueError("frame is not CTLST Widget Protocol v1")
    if frame.get("type") != "frame":
        raise ValueError("first response must be a frame")
    view = frame.get("view")
    if not isinstance(view, dict):
        raise ValueError("frame.view must be an object")
    width = float(view.get("width", 0))
    height = float(view.get("height", 0))
    if not 0 < width <= 4096 or not 0 < height <= 4096:
        raise ValueError("view dimensions must be within 1..4096")
    nodes = frame.get("nodes")
    if not isinstance(nodes, list) or len(nodes) > 512:
        raise ValueError("nodes must be an array with at most 512 entries")
    for index, node in enumerate(nodes):
        if not isinstance(node, dict) or node.get("type") not in NODE_TYPES:
            raise ValueError(f"unsupported node at index {index}")
        for field in ("fill", "stroke"):
            if field in node:
                color = node[field]
                if not isinstance(color, str) or not (
                    color in THEME_TOKENS or HEX_COLOR.fullmatch(color)
                ):
                    raise ValueError(
                        f"node {index} has an invalid {field}; use a #RRGGBB[A] "
                        "color or a shared @ctlst_* token"
                    )
    background = view.get("background")
    if background is not None and (
        not isinstance(background, str)
        or not (background in THEME_TOKENS or HEX_COLOR.fullmatch(background))
    ):
        raise ValueError(
            "view.background must be a #RRGGBB[A] color or a shared "
            "@ctlst_* token"
        )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("descriptor", type=Path)
    parser.add_argument("--exec", dest="executable")
    parser.add_argument("--timeout", type=float, default=2.0)
    args = parser.parse_args()
    data = parse_descriptor(args.descriptor)
    check_descriptor(data)
    executable = args.executable or shutil.which(data["exec"])
    if not executable:
        parser.error(f"executable not found: {data['exec']}")
    process = subprocess.Popen(
        [executable],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )
    try:
        hello = {
            "ctlst_widget": 1,
            "type": "hello",
            "id": data["id"],
            "width": 240,
            "height": 240,
            "orientation": "portrait",
        }
        assert process.stdin is not None and process.stdout is not None
        process.stdin.write(json.dumps(hello) + "\n")
        process.stdin.flush()
        ready, _, _ = select.select([process.stdout], [], [], args.timeout)
        if not ready:
            raise ValueError("helper did not emit a frame before timeout")
        line = process.stdout.readline()
        if len(line.encode("utf-8")) > 256 * 1024:
            raise ValueError("frame exceeds 256 KiB")
        check_frame(json.loads(line))
        process.stdin.write('{"ctlst_widget":1,"type":"shutdown"}\n')
        process.stdin.flush()
        process.wait(timeout=args.timeout)
        if process.returncode != 0:
            raise ValueError(f"helper exited with {process.returncode}")
    finally:
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                process.kill()
    print(f"valid ctlst-widget-1: {data['id']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
