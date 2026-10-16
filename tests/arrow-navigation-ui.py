#!/usr/bin/env python3
"""Real GTK key routing with inert controls; run in an isolated Wayland VM."""
import ast
import os
from pathlib import Path
import subprocess
import time
from types import MethodType, SimpleNamespace

import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gio, GLib, Gtk

ROOT = Path(__file__).resolve().parents[1]


def method(script, name):
    tree = ast.parse((ROOT / "scripts" / script).read_text())
    node = next(node for node in ast.walk(tree)
                if isinstance(node, ast.FunctionDef) and node.name == name)
    scope = {"Gdk": Gdk, "Gtk": Gtk}
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(script), "exec"), scope)
    return scope[name]


def settle():
    deadline = time.monotonic() + .25
    while time.monotonic() < deadline:
        GLib.MainContext.default().iteration(False)
        time.sleep(.001)


def focus_window():
    env = dict(os.environ, SWAYSOCK=str(next(Path(os.environ["XDG_RUNTIME_DIR"]).glob("sway-ipc.*.sock"))))
    subprocess.run(["wtype", "-s", "50"], check=True)
    subprocess.run(["swaymsg", '[title="Arrow navigation fixture"] focus'], env=env, check=True)
    settle()


def key(name):
    # Keep the GTK client dispatching while the virtual keyboard acquires the
    # seat; blocking this process in subprocess.run can lose the focus edge.
    process = subprocess.Popen(["wtype", "-s", "150", "-k", name])
    deadline = time.monotonic() + 3
    while process.poll() is None:
        assert time.monotonic() < deadline
        GLib.MainContext.default().iteration(False)
        time.sleep(.001)
    assert process.returncode == 0
    settle()


Gtk.init()
window = Gtk.Window()
window.set_title("Arrow navigation fixture")
window.set_default_size(400, 400)
root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
root.set_focusable(True)
window.set_child(root)
window.search = Gtk.SearchEntry()
root.append(window.search)
window.initial_focus = root
window.filtered = Gtk.StringList.new(["One", "Two", "Three", "Four"])
selection = Gtk.SingleSelection(model=window.filtered)
selection.set_autoselect(False)
selection.set_can_unselect(True)
selection.set_selected(Gtk.INVALID_LIST_POSITION)
factory = Gtk.SignalListItemFactory()
factory.connect("setup", lambda _, item: item.set_child(Gtk.Label()))
factory.connect("bind", lambda _, item: item.get_child().set_label(item.get_item().get_string()))
window.grid = Gtk.GridView(model=selection, factory=factory)
window.grid.set_min_columns(2)
window.grid.set_max_columns(2)
root.append(window.grid)
window.on_search_changed = lambda _: None
activated = []
window.launch_app = lambda _, position: activated.append(position)
window.grid.connect("activate", window.launch_app)
window.request_hide = lambda: None
window.search_engaged = False
keys = Gtk.EventControllerKey()
keys.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
keys.connect("key-pressed", MethodType(method("ctlstdrawer-ui", "on_key_pressed"), window))
window.add_controller(keys)
window.present()
settle()
focus_window()
root.grab_focus()
key("Down")
settle()
assert selection.get_selected() == 0, (selection.get_selected(), window.get_focus())
key("Right")
settle()
assert selection.get_selected() == 1, selection.get_selected()
key("Return")
settle()
assert activated == [1], activated
window.search.grab_focus()
window.search.set_text("abcd")
window.search.set_position(2)
key("Left")
settle()
assert window.search.get_position() == 1
print("PASS: drawer Down selects first, Right selects second, Enter activates second, search caret retained")
window.destroy()

window = Gtk.Window()
window.set_title("Arrow navigation fixture")
row = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
first, second = Gtk.Button(label="First"), Gtk.Button(label="Second")
row.append(first)
row.append(second)
window.set_child(row)
fixture = SimpleNamespace(props=SimpleNamespace(active_window=window), current_page="index")
keys = Gtk.EventControllerKey()
keys.connect("key-pressed", MethodType(method("ctlst-settings", "navigation_key"), fixture))
window.add_controller(keys)
window.present()
settle()
focus_window()
first.grab_focus()
key("Right")
settle()
assert window.get_focus() == second
key("Left")
settle()
assert window.get_focus() == first
print("PASS: Settings spatial arrows traverse native controls")
window.destroy()
