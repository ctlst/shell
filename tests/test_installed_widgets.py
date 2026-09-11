import json
import os
from pathlib import Path
import select
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
EXAMPLE = ROOT / "examples/pocket-note/ctlst-widget-pocket-note"


def test_widget_tools_stage_without_native_build(tmp_path):
    subprocess.run(["make", "install-widget-tools", f"DESTDIR={tmp_path}"],
                   cwd=ROOT, check=True, capture_output=True)
    env = os.environ | {"CTLST_LIBEXEC_DIR": str(tmp_path / "usr/libexec/ctlst-shell"),
                        "CTLST_DATA_DIR": str(tmp_path / "usr/share/ctlst-shell")}
    cli = str(tmp_path / "usr/bin/ctlst-widget")
    guide = subprocess.check_output([cli, "guide"], env=env, text=True)
    assert "Validation **executes the helper as you**" in guide
    assert "no particular agent" in guide
    assert not (tmp_path / "usr/bin/ctlsthome").exists()
    assert os.access(tmp_path / "usr/bin/ctlst-home-notes", os.X_OK)
    assert "dev.ctlst.Notes.desktop" in (tmp_path / "usr/share/ctlst/widgets/notes.yaml").read_text()
    assert "Exec=ctlst-home-notes --edit" in (tmp_path / "usr/share/applications/dev.ctlst.Notes.desktop").read_text()
    assert (tmp_path / "usr/share/ctlst-shell/docs/NOTES.md").is_file()
    descriptor = tmp_path / "usr/share/ctlst-shell/examples/pocket-note/pocket-note.yaml"
    subprocess.run([cli, "validate", str(descriptor)], env=env, check=True)
    subprocess.run([cli, "register", str(descriptor), "--config-dir", str(tmp_path / "config"),
                    "--bin-dir", str(tmp_path / "user bin")], env=env, check=True)
    installed = (tmp_path / "config/widgets/pocket-note.yaml").read_text()
    assert f'exec: "{tmp_path}/user bin/ctlst-widget-pocket-note"' in installed


def test_pocket_note_live_edits_visibility_theme_and_shutdown(tmp_path):
    config = tmp_path / "config/ctlst"
    config.mkdir(parents=True)
    generated = tmp_path / "generated"
    generated.mkdir()
    (generated / "theme.env").write_text("SHELL_TEXT=112233ff\nSHELL_ACCENT=abcdef\n")
    env = os.environ | {"XDG_CONFIG_HOME": str(tmp_path / "config"),
                        "CTLST_GENERATED_DIR": str(generated)}
    process = subprocess.Popen([sys.executable, str(EXAMPLE)], env=env,
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE)

    def send(kind, **fields):
        process.stdin.write((json.dumps(dict(ctlst_widget=1, type=kind, **fields)) + "\n").encode())
        process.stdin.flush()

    def receive():
        assert select.select([process.stdout], [], [], 3)[0]
        return json.loads(process.stdout.readline())

    try:
        send("hello")
        first = receive()
        assert first["nodes"][2]["fill"] == "#112233ff"
        assert not select.select([process.stdout], [], [], 1.2)[0], "idle widget emitted an unchanged frame"
        (config / "pocket-note.txt").write_text("Changed without a rebuild\nSecond line\nIgnored line")
        changed = receive()
        assert changed["nodes"][2]["text"] == "Changed without a rebuild"
        send("visibility", visible=False)
        (config / "pocket-note.txt").write_text("Hidden update")
        assert not select.select([process.stdout], [], [], 1.2)[0]
        send("visibility", visible=True)
        assert receive()["nodes"][2]["text"] == "Hidden update"
        (generated / "theme.env").write_text("SHELL_TEXT=aabbccff\n")
        assert receive()["nodes"][2]["fill"] == "#aabbccff"
        send("configure", width=500, height=100, orientation="landscape")
        assert receive()["view"]["width"] == 500
        send("shutdown")
        assert process.wait(timeout=3) == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=3)


def test_widget_reload_requires_a_current_session():
    env = os.environ | {"PYTHONPATH": str(ROOT / "scripts")}
    env.pop("CTLST_SESSION_ID", None)
    result = subprocess.run([sys.executable, str(ROOT / "scripts/ctlst-widget-reload")],
                            env=env, capture_output=True, text=True)
    assert result.returncode != 0
    assert "inside the CTLST session" in result.stderr
