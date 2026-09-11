#!/usr/bin/env python3
"""Scaffold a CTLST Widget Protocol v1 helper and descriptor."""

from __future__ import annotations

import argparse
import re
import stat
from pathlib import Path


ID = re.compile(r"[a-z0-9][a-z0-9_-]*\Z")


HELPER = '''#!/usr/bin/env python3
"""{name}: CTLST Widget Protocol v1 reference helper."""

import json
import sys

counter = 0
visible = True
pointer_armed = False


def send_frame():
    nodes = [
        {{"type": "text", "x": 50, "y": 18, "text": {name!r},
         "size": 12, "weight": "bold", "align": "center",
         "fill": "@ctlst_text"}},
        {{"type": "rect", "x": 18, "y": 42, "width": 64, "height": 34,
         "radius": 17, "fill": "@ctlst_accent"}},
        {{"type": "text", "x": 50, "y": 49, "text": str(counter),
         "size": 16, "weight": "bold", "align": "center",
         "fill": "@ctlst_selected_text"}},
    ]
    frame = {{
        "ctlst_widget": 1,
        "type": "frame",
        "seq": counter,
        "view": {{"width": 100, "height": 100, "background": "@ctlst_panel"}},
        "nodes": nodes,
    }}
    print(json.dumps(frame, separators=(",", ":")), flush=True)


for raw in sys.stdin:
    # Keep protocol parsing independent from the rendering/event loop in larger
    # widgets; this compact scaffold intentionally uses a blocking loop.
    try:
        message = json.loads(raw)
    except json.JSONDecodeError:
        continue
    if message.get("ctlst_widget") != 1:
        continue
    kind = message.get("type")
    if kind in ("hello", "configure"):
        send_frame()
    elif kind == "visibility":
        visible = bool(message.get("visible"))
        if visible:
            send_frame()
    elif kind == "pointer":
        phase = message.get("phase")
        if phase == "begin":
            pointer_armed = bool(message.get("inside", True))
        elif phase in ("cancel",):
            pointer_armed = False
        elif phase == "end":
            # CTLST touch rule: mutate only on an inside release after an
            # inside begin, never on press.
            if pointer_armed and message.get("inside", True):
                counter += 1
                send_frame()
            pointer_armed = False
    elif kind == "shutdown":
        break
'''


DESCRIPTOR = '''id: {widget_id}
name: {name}
kind: exec
protocol: ctlst-widget-1
interaction: pointer
min_columns: {columns}
min_rows: {rows}
default_columns: {columns}
default_rows: {rows}
exec: ctlst-widget-{widget_id}
args: []
'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("widget_id")
    parser.add_argument("--name")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--columns", type=int, default=2)
    parser.add_argument("--rows", type=int, default=2)
    parser.add_argument("--force", action="store_true")
    args = parser.parse_args()
    if not ID.fullmatch(args.widget_id):
        parser.error("widget ID must match [a-z0-9][a-z0-9_-]*")
    if not 1 <= args.columns <= 5 or not 1 <= args.rows <= 4:
        parser.error("widget span must fit the 5x4 Home cluster")
    name = args.name or args.widget_id.replace("-", " ").replace("_", " ").title()
    target = args.output / args.widget_id
    helper = target / f"ctlst-widget-{args.widget_id}"
    descriptor = target / f"{args.widget_id}.yaml"
    target.mkdir(parents=True, exist_ok=True)
    for path in (helper, descriptor):
        if path.exists() and not args.force:
            parser.error(f"refusing to overwrite {path}; pass --force deliberately")
    helper.write_text(HELPER.format(name=name), encoding="utf-8")
    helper.chmod(helper.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    descriptor.write_text(
        DESCRIPTOR.format(
            widget_id=args.widget_id,
            name=name,
            columns=args.columns,
            rows=args.rows,
        ),
        encoding="utf-8",
    )
    print(helper)
    print(descriptor)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
