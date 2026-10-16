"""Close escalation safety; never signals a real process."""
import importlib.util
from pathlib import Path
import types
import os
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("close_test", ROOT / "scripts/ctlst_close.py")
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def node(pid=42, node_id=9, app="cursor", focused=True):
    return dict(id=node_id, pid=pid, app_id=app, focused=focused, visible=True,
                type="con", nodes=[], floating_nodes=[])


def tree(window):
    return dict(type="workspace", name="app:7", nodes=[], floating_nodes=[window])


def test_floating_dialog_capture_and_focus_change():
    chosen = c.target(tree(node()))
    assert chosen == dict(id=9, pid=42, app="cursor", visible=True, workspace="app:7")
    assert c.same_target(chosen, c.target(tree(node(focused=False)), 9))
    assert not c.same_target(chosen, c.target(tree(node(pid=43)), 9))
    assert not c.same_target(chosen, c.target(tree(node(node_id=10)), 10))
    assert c.target(tree(node(pid=None))) is None


def test_xwayland_class_and_missing_properties():
    window = node(app=None)
    window["window_properties"] = {"class": "Cursor"}
    assert c.target(tree(window))["app"] == "Cursor"
    window["window_properties"] = None
    assert c.target(tree(window))["app"] == "Application"


@pytest.mark.parametrize("app", sorted(c.PROTECTED_APPS))
def test_core_services_cannot_be_force_quit(app, monkeypatch):
    monkeypatch.setattr(c, "process_identity", lambda *_: pytest.fail("must reject before inspecting process"))
    assert c.PinnedProcess(dict(pid=42, app=app)).fd is None


@pytest.mark.parametrize("identity", [(123456789, 1, "cursor", "cursor"),
                                      (c.os.getuid(), 1, "sway", "sway")])
def test_other_users_and_compositor_rejected(identity, monkeypatch):
    monkeypatch.setattr(c, "process_identity", lambda *_: identity)
    assert c.PinnedProcess(dict(pid=42, app="fake")).fd is None


def test_force_uses_pinned_pidfd_and_original_window(monkeypatch):
    chosen = dict(pid=42, id=9, app="cursor")
    pinned = c.PinnedProcess.__new__(c.PinnedProcess)
    pinned.fd, pinned.chosen = 123, chosen
    pinned.identity = (c.os.getuid(), 1, "cursor", "cursor")
    monkeypatch.setattr(c, "process_identity", lambda *_: pinned.identity)
    monkeypatch.setattr(pinned, "alive", lambda: True)
    signals = []
    monkeypatch.setattr(c.signal, "pidfd_send_signal", lambda *args: signals.append(args), raising=False)
    assert not pinned.force(chosen | {"pid": 99})
    assert not pinned.force(chosen | {"id": 99})
    assert signals == []
    assert pinned.force(chosen)
    assert signals == [(123, c.signal.SIGKILL)]


@pytest.mark.parametrize("event", [dict(change="close", container={"id": 9}),
                                   dict(change="focus", container={"id": 10, "pid": 43})])
def test_closed_window_or_different_app_cancels(event):
    assert c.event_cancels(0x80000003, event, dict(id=9, pid=42))
    assert c.event_cancels(0x80000006, {}, dict(id=9, pid=42))


def test_same_app_dialog_and_unrelated_close_do_not_cancel():
    chosen = dict(id=9, pid=42)
    assert not c.event_cancels(0x80000003, dict(change="focus", container={"id": 10, "pid": 42}), chosen)
    assert not c.event_cancels(0x80000003, dict(change="close", container={"id": 10}), chosen)


def test_timeout_alone_never_force_quits(tmp_path, monkeypatch):
    calls = []
    ipc = types.SimpleNamespace(request=lambda kind, payload="": tree(node()) if kind == 4 else [])
    class Events:
        def __enter__(self): return self
        def __exit__(self, *_): pass
    ipc.subscribe = Events
    monkeypatch.setattr(c, "preferences", lambda: (1, 5))
    monkeypatch.setattr(c, "watch", lambda *_: True)
    monkeypatch.setattr(c, "confirm", lambda *_: False)
    monkeypatch.setattr(c, "PinnedProcess", lambda *_: types.SimpleNamespace(
        alive=lambda: True, close=lambda: None, force=lambda *_: calls.append("force")))
    c.close_window(ipc, tmp_path)
    assert calls == []


def test_locked_screen_makes_no_ipc_requests(tmp_path):
    (tmp_path / "ctlstlock.visible").touch()
    ipc = types.SimpleNamespace(request=lambda *_: pytest.fail("locked"))
    c.close_window(ipc, tmp_path)


def test_close_config_units_and_bounds(tmp_path, monkeypatch):
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    assert c.preferences() == (3, 45)
    (tmp_path / "ctlst").mkdir()
    (tmp_path / "ctlst/close.conf").write_text("[close]\ngrace_seconds=1\nconfirmation_seconds=10\n")
    assert c.preferences() == (1, 10)
    (tmp_path / "ctlst/close.conf").write_text("[close]\ngrace_seconds=nan\n")
    with pytest.raises(ValueError):
        c.preferences()


def test_quick_action_routes_the_captured_id_to_shared_close_helper():
    source = (ROOT / "ctlst-gestured/ctlst-gestured.c").read_text()
    body = source.split("close_focused_app(struct daemon_state *state)", 1)[1].split("static int\nshell_bottom_bar_height", 1)[0]
    assert "close-active --node %" in body
    assert "--return-home" in body
    assert "] kill; workspace number 1" not in body


@pytest.mark.parametrize("visible", [False, True])
def test_wrapper_does_not_swallow_close_when_drawer_already_hidden(tmp_path, visible):
    scripts = tmp_path / "home/.local/share/sway-touch/scripts"
    scripts.mkdir(parents=True)
    commands = tmp_path / "commands"
    commands.mkdir()
    for path, text in ((scripts / "ctlstdrawer", "#!/bin/sh\nexit 0\n"),
                       (commands / "pgrep", "#!/bin/sh\nexit 1\n")):
        path.write_text(text)
        path.chmod(0o755)
    (scripts / "ctlst_close.py").write_text("import sys; print('helper', *sys.argv[1:])\n")
    if visible:
        (tmp_path / "ctlstdrawer.visible").touch()
    result = subprocess.run([str(ROOT / "scripts/close-active"), "--node", "9"],
        env=os.environ | {"HOME": str(tmp_path / "home"), "XDG_RUNTIME_DIR": str(tmp_path),
                          "CTLST_LIBEXEC_DIR": str(scripts),
                          "PATH": str(commands) + os.pathsep + os.environ["PATH"]},
        text=True, capture_output=True, check=True)
    assert result.stdout == ("" if visible else "helper --node 9\n")
