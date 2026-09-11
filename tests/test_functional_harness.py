"""Exercise the accessibility readiness predicate without requiring a GUI."""
import ast
from pathlib import Path
from types import SimpleNamespace

import pytest

ROOT = Path(__file__).resolve().parents[1]


@pytest.mark.parametrize("sensitive,showing,activated", [
    (True, True, True), (False, True, False), (True, False, False),
])
def test_button_accepts_gtk_states_without_enabled(sensitive, showing, activated):
    source = ast.parse((ROOT / "vm/clean-room/functional.py").read_text())
    function = next(n for n in source.body if isinstance(n, ast.FunctionDef)
                    and n.name == "ui_button")
    calls = []
    action = SimpleNamespace(get_n_actions=lambda: 1,
                             do_action=lambda index: calls.append(index) or True)
    states = SimpleNamespace(contains=lambda flag: {"sensitive": sensitive,
                                                    "showing": showing}[flag])
    node = SimpleNamespace(get_state_set=lambda: states,
                           get_name=lambda: "Sound details",
                           get_action_iface=lambda: action)
    outcomes = []
    scope = {"accessible_nodes": lambda: [node],
             "wait": lambda label, predicate: outcomes.append(predicate()),
             "Atspi": SimpleNamespace(StateType=SimpleNamespace(
                 SENSITIVE="sensitive", SHOWING="showing"))}
    exec(compile(ast.Module(body=[function], type_ignores=[]), "fixture", "exec"), scope)
    scope["ui_button"]("Sound details")
    assert outcomes == [activated]
    assert calls == ([0] if activated else [])


def test_wallpaper_sequence_waits_for_ui_completion_not_initial_selection():
    source = (ROOT / "vm/clean-room/functional.py").read_text()
    assert source.index('ui_button("Apply wallpaper")') < source.index(
        'wait("Settings finished saving wallpaper"') < source.index(
        'ui_button("Choose from files")')
    assert source.index('wait("native wallpaper chooser opened"') < source.index(
        'key("l", ("ctrl",))') < source.index('ui_button("Open")') < source.index(
        'wait("native wallpaper chooser closed"')
