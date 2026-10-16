#!/usr/bin/env python3
"""Isolated native GTK regression. Requires a disposable nested/headless Sway."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

base = Path(tempfile.mkdtemp(prefix="ctlst-battery-ui-"))
os.environ["XDG_STATE_HOME"] = str(base / "state")
os.environ["XDG_CONFIG_HOME"] = str(base / "config")
os.environ["SWAYSOCK"] = str(next(Path(os.environ["XDG_RUNTIME_DIR"]).glob("sway-ipc.*.sock")))
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from ctlst_battery import Store, state_dir
from ctlst_battery_ui import Battery, Gtk, GLib

store = Store()
now = time.time()
for index in range(181):
    stamp = now - 10800 + index * 60
    row = dict(timestamp=stamp, elapsed=stamp, awake=stamp, boot="fixture", interval=60,
               device="Fixture (not hardware)", status="Discharging", capacity=100 - index / 6,
               watts=2 + (index % 10) / 10, temp=27, screen=index % 2,
               brightness=40, process_activity=True)
    store.append(row, [("long-process-name-" * 4, .5)])
store.db.close()
app = Battery()
errors = []
sizes = iter(((640, 960), (960, 1920), (1920, 960), (1280, 960)))
frame = 0


def step():
    global frame
    try:
        window = app.props.active_window
        if not window or app.data is None:
            raise AssertionError("Battery window/history did not load")
        assert app.data["latest"]["device"] == "Fixture (not hardware)"
        assert "Not observed yet" not in app.full.get_text()
        # No child may force the window past the output's available width.
        outputs = __import__("json").loads(subprocess.check_output(["swaymsg", "-r", "-t", "get_outputs"]))
        width = outputs[0]["rect"]["width"]
        assert window.get_width() <= width, (window.get_width(), width)
        assert app.chart.get_width() > 100
        assert app.chart.get_width() < width
        subprocess.run(["grim", str(base / f"battery-{frame}.png")], check=True)
        frame += 1
        try:
            w, h = next(sizes)
        except StopIteration:
            app.ranges.set_selected(4)
            GLib.timeout_add(300, finish)
            return False
        subprocess.run(["swaymsg", "output", "HEADLESS-1", "mode", f"{w}x{h}", "scale", "2"], check=True, stdout=subprocess.DEVNULL)
        GLib.timeout_add(700, step)
    except Exception as exc:
        errors.append(str(exc))
        app.quit()
    return False


def finish():
    try:
        assert len(app.data["samples"]) > 100
        assert app.ranges.get_selected() == 4
        assert app.timer is not None
        app.props.active_window.close()
        assert app.timer is None
    except Exception as exc:
        errors.append(str(exc))
    app.quit()
    return False


GLib.timeout_add(1800, step)
GLib.timeout_add_seconds(20, lambda: (errors.append("Timeout"), app.quit(), False)[-1])
app.run([])
print("Battery UI evidence:", base)
assert not errors, errors
print("PASS: native GTK history load, ranges, 320/480/640/960px layouts, rotation and close/timer cleanup")
