"""Real note IO/editor arguments and widget lifecycle, all under tmp_path."""
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / "scripts/ctlst-home-notes"


def module():
    loader = importlib.machinery.SourceFileLoader("notes_test", str(SCRIPT))
    spec = importlib.util.spec_from_loader(loader.name, loader)
    result = importlib.util.module_from_spec(spec)
    loader.exec_module(result)
    return result


def environment(tmp_path):
    return os.environ | {
        "XDG_CONFIG_HOME": str(tmp_path / "config"), "CTLST_DATA_DIR": str(tmp_path / "data"),
        "CTLST_PROFILE_DIR": str(tmp_path / "profiles"), "CTLST_SYSTEM_CONFIG_DIR": str(tmp_path / "etc")}


def test_editor_saves_exact_note_path_without_shell_evaluation(tmp_path):
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    editor = tmp_path / "editor.py"
    editor.write_text("import pathlib,sys\nassert len(sys.argv)==2\npathlib.Path(sys.argv[1]).write_text('Saved note')\n")
    filename = "a note ; $not-a-command.txt"
    (config / filename).write_text("Existing")
    (config / "notes.conf").write_text(
        "[notes]\nfile = " + filename + "\neditor = " + json.dumps([sys.executable, str(editor)]) + "\n")
    env = environment(tmp_path)
    env.pop("SWAYSOCK", None)
    subprocess.run([sys.executable, str(SCRIPT), "--edit"], env=env, check=True, timeout=5)
    assert (config / filename).read_text() == "Saved note"


def test_preview_bounds_nonfiles_and_invalid_bytes(tmp_path):
    notes = module()
    filename = tmp_path / "note"
    filename.write_bytes(b"Hello\x00\xff\n" + b"a" * 20000)
    text, footer = notes.preview(filename)
    assert len(text) <= 8192 and "\x00" not in text
    assert footer == "Tap to edit"
    for width, height in ((80,70), (140,180), (400,110), (400,400)):
        frame = notes.frame(0, width, height, text, footer)
        assert len(frame["nodes"]) <= 42
        assert len(json.dumps(frame)) < 262144
        assert all(0 <= n["y"] < height for n in frame["nodes"])
        assert frame["view"]["background"] == "@ctlst_panel"
    fifo = tmp_path / "fifo"
    os.mkfifo(fifo)
    assert notes.preview(fifo)[0] == "Notes path is not a file"
    assert notes.preview(tmp_path / "missing")[0] == "No notes yet"


def test_missing_editor_does_not_create_note(tmp_path, monkeypatch):
    notes = module()
    monkeypatch.setattr(notes, "settings", lambda: (tmp_path / "note", ["no-such-editor"]))
    monkeypatch.setattr(notes.shutil, "which", lambda _: None)
    try:
        notes.edit_note()
    except ValueError as error:
        assert "not installed" in str(error)
    else:
        raise AssertionError("Missing editor must fail clearly")
    assert not (tmp_path / "note").exists()


def test_phone_terminal_fallback_has_notes_app_identity(monkeypatch):
    notes = module()
    monkeypatch.setattr(notes.shutil, "which", lambda name: name if name in ("foot", "nvim") else None)
    assert notes.editor_command([]) == ["foot", "--app-id=dev.ctlst.Notes", "-e", "nvim"]


def test_terminal_mime_handler_uses_explicit_foot(tmp_path, monkeypatch):
    notes = module()
    apps = tmp_path / "applications"
    apps.mkdir()
    (apps / "nvim.desktop").write_text("[Desktop Entry]\nExec=nvim %F\nTerminal=true\n")
    monkeypatch.setenv("XDG_DATA_HOME", str(tmp_path))
    monkeypatch.setenv("XDG_DATA_DIRS", str(tmp_path))
    monkeypatch.setattr(notes.shutil, "which", lambda name: name if name in ("xdg-mime", "gio", "foot", "nvim") else None)
    monkeypatch.setattr(notes.subprocess, "check_output", lambda *a, **k: "nvim.desktop\n")
    assert notes.editor_command([]) == ["foot", "--app-id=dev.ctlst.Notes", "-e", "nvim"]


def test_note_live_changes_hidden_idle_resize_and_shutdown(tmp_path):
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    note = config / "notes.txt"
    process = subprocess.Popen([sys.executable, str(SCRIPT)], env=environment(tmp_path),
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    def send(kind, **fields):
        process.stdin.write((json.dumps(dict(ctlst_widget=1, type=kind, **fields)) + "\n").encode())
        process.stdin.flush()
    def receive():
        assert select.select([process.stdout], [], [], 3)[0]
        return json.loads(process.stdout.readline())
    try:
        send("hello", width=240, height=240)
        first = receive()
        assert any(n.get("text") == "No notes yet" for n in first["nodes"])
        assert not note.exists(), "A preview must not create/overwrite a note"
        assert not select.select([process.stdout], [], [], 1.2)[0]
        note.write_text("Saved by editor")
        assert any(n.get("text") == "Saved by editor" for n in receive()["nodes"])
        send("visibility", visible=False)
        note.write_text("Changed while hidden")
        assert not select.select([process.stdout], [], [], 1.2)[0]
        send("visibility", visible=True)
        assert any(n.get("text") == "Changed while hidden" for n in receive()["nodes"])
        send("configure", width=360, height=110)
        assert receive()["view"] == {"width":360,"height":110,"background":"@ctlst_panel"}
        (config / "notes.conf").write_text("[notes]\neditor=not-json\n")
        assert any(n.get("text") == "Notes configuration error" for n in receive()["nodes"])
        send("shutdown")
        assert process.wait(timeout=3) == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


def test_packaged_descriptor_has_real_editor_and_path_contract():
    descriptor = (ROOT / "widgets/notes.yaml").read_text()
    assert "interaction: launch" in descriptor
    assert "desktop: dev.ctlst.Notes.desktop" in descriptor
    assert "Exec=ctlst-home-notes --edit" in (ROOT / "dev.ctlst.Notes.desktop").read_text()
    runtime = (ROOT / "ctlsthome/home-widget-runtime.c").read_text()
    assert 'g_signal_connect(runtime->view, "resize"' in runtime
    assert '"notify::width"' not in runtime
