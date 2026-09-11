#!/usr/bin/env python3
"""Functional acceptance inside a disposable guest, against installed files only."""
import json
import configparser
import os
from pathlib import Path
import signal
import socket
import stat
import subprocess
import time
import argparse

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--disposable-vm", required=True, action="store_true",
                    help="confirm an isolated test guest and expendable test user")
parser.parse_args()  # Refuse before loading GTK or writing any fixture state.

import gi
gi.require_version("GdkPixbuf", "2.0")
gi.require_version("Atspi", "2.0")
from gi.repository import Atspi, GdkPixbuf, GLib
Atspi.set_timeout(1000, 1000)

RUNTIME = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
ARTIFACTS = Path.home() / "ctlst-functional-results"
LIBEXEC = Path("/usr/libexec/ctlst-shell")
ARTIFACTS.mkdir(exist_ok=True)
results = []
session = None
CONFIG = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "ctlst"
original_dotfiles = {name: (CONFIG / name).read_bytes() if (CONFIG / name).is_file() else None
                     for name in ("sway.conf", "input.conf", "theme.conf", "memory.conf", "preferences.json")}


def run(*args, **kwargs):
    result = subprocess.run(args, text=True, capture_output=True, timeout=30, **kwargs)
    if result.returncode:
        raise RuntimeError(f"{args!r}: {result.stdout}\n{result.stderr}")
    return result.stdout


def wait(label, predicate, seconds=30):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        value = predicate()
        if value:
            return value
        if session and session.poll() is not None:
            raise RuntimeError(f"session exited while waiting for {label}")
        time.sleep(0.15)
    raise RuntimeError(f"timed out: {label}")


def passed(label):
    results.append(label)
    print("PASS:", label, flush=True)


def sway(kind):
    return json.loads(run("swaymsg", "-r", "-t", kind))


def nodes(node):
    yield node
    for child in node.get("nodes", []) + node.get("floating_nodes", []):
        yield from nodes(child)


def app(app_id):
    return next((n for n in nodes(sway("get_tree")) if n.get("app_id") == app_id), None)


def workspace():
    return next(w["name"] for w in sway("get_workspaces") if w["focused"])


def key(name, modifiers=()):
    args = ["wtype"]
    for mod in modifiers:
        args += ["-M", mod]
    args += ["-k", name]
    for mod in reversed(modifiers):
        args += ["-m", mod]
    run(*args)
    time.sleep(0.4)


def screenshot(name):
    time.sleep(0.7)
    run("grim", str(ARTIFACTS / f"{name}.png"))


def pixel(name, x, y):
    picture = GdkPixbuf.Pixbuf.new_from_file(str(ARTIFACTS / f"{name}.png"))
    offset = y * picture.get_rowstride() + x * picture.get_n_channels()
    return tuple(picture.get_pixels()[offset:offset + 3])


def accessible_nodes(root=None, depth=0):
    root = root or Atspi.get_desktop(0)
    if depth > 24:
        return
    yield root
    try:
        for index in range(root.get_child_count()):
            child = root.get_child_at_index(index)
            if child:
                yield from accessible_nodes(child, depth + 1)
    except GLib.Error:
        return


def ui_button(name):
    def press():
        for node in accessible_nodes():
            states = node.get_state_set()
            if node.get_name() == name and all(states.contains(flag) for flag in (
                    Atspi.StateType.SENSITIVE, Atspi.StateType.SHOWING)):
                # GTK4 reports SENSITIVE/SHOWING but may omit ENABLED even
                # for usable buttons. Keep the busy/hidden guards without
                # requiring an accessibility state the toolkit does not emit.
                action = node.get_action_iface()
                if action and action.get_n_actions():
                    return action.do_action(0)
        return False
    wait(f"accessible button {name}", press)


def select_theme_in_settings(name):
    key("Left", ("alt",))
    ui_button("Appearance")
    current = run(str(LIBEXEC / "ctlst-theme"), "current").strip()
    def open_selector():
        for node in accessible_nodes():
            # GTK exposes the selected item as the combo's name. Its nested
            # toggle has the actual activation action (the combo has none).
            if node.get_role() == Atspi.Role.COMBO_BOX and node.get_name() in (current, "Shell theme selector"):
                for child in accessible_nodes(node):
                    if child.get_role() == Atspi.Role.TOGGLE_BUTTON:
                        action = child.get_action_iface()
                        if action and action.get_n_actions():
                            return action.do_action(0)
        return False
    wait("theme selector opened", open_selector)
    screenshot("theme-picker-open")
    themes = run(str(LIBEXEC / "ctlst-theme"), "list").splitlines()
    key("Home")
    for _ in range(themes.index(name) + 1):  # First row is the non-writing prompt.
        key("Down")
    key("Return")
    ui_button("Apply theme")
    wait("selected theme applied by Settings", lambda: run(str(LIBEXEC / "ctlst-theme"), "current").strip() == name)


def wallpaper_state():
    return json.loads(run(str(LIBEXEC / "ctlst-preferences"), "wallpaper", "get"))


def bar_live():
    try:
        record = json.loads((RUNTIME / "ctlst-bar.json").read_text())
        return (Path("/proc") / str(record["pid"]) / "comm").read_text().strip() == "waybar"
    except (FileNotFoundError, json.JSONDecodeError):
        return False


def pointer_at(x, y):
    # Real motion delivers pointer enter; IPC cursor warping alone can leave
    # the previous surface as the axis target until a motion event arrives.
    run("wlrctl", "pointer", "move", "-10000", "-10000")
    run("wlrctl", "pointer", "move", str(x), str(y))
    time.sleep(0.2)


def start_session(index):
    global session
    for key_name in ("SWAYSOCK", "WAYLAND_DISPLAY"):
        os.environ.pop(key_name, None)
    log = (ARTIFACTS / f"session-{index}.log").open("w")
    environment = os.environ | {"WLR_BACKENDS": "headless,libinput", "LIBSEAT_BACKEND": "seatd",
                                "WLR_RENDERER": "pixman", "GSK_RENDERER": "cairo",
                                "CTLST_SETTINGS_DEBUG_LOG": str(ARTIFACTS / "settings-ready.log")}
    session = subprocess.Popen(["ctlst-session", "start"], env=environment, stdout=log, stderr=log)
    log.close()
    sock = wait("Sway IPC", lambda: next((p for p in RUNTIME.glob("sway-ipc.*.sock") if p.is_socket()), None))
    os.environ["SWAYSOCK"] = str(sock)
    display = wait("Wayland display", lambda: next((p for p in RUNTIME.glob("wayland-*") if p.is_socket()), None))
    os.environ["WAYLAND_DISPLAY"] = display.name
    for name in ("home", "drawer", "dock", "overview", "shade", "action", "gestures", "workspaces", "bar"):
        def endpoint_live(name=name):
            if name == "workspaces":
                return subprocess.run([str(LIBEXEC / "ctlst-workspaced"), "list"],
                                      capture_output=True, timeout=3).returncode == 0
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
                    client.sendto(b"?", str(RUNTIME / f"ctlst-{name}.sock"))
                return True
            except (FileNotFoundError, ConnectionRefusedError):
                return False
        wait(name, endpoint_live)
    run("swaymsg", "output HEADLESS-1 mode 720x1080")
    time.sleep(2)
    wait("Waybar child running", bar_live)
    wait("status bar reserves its visible top edge", lambda: any(w["rect"]["y"] >= 30 for w in sway("get_workspaces")))


def stop_session():
    global session
    # Sway may close IPC before acknowledging exit. Its actual exit status is
    # the authority; a missing reply alone is not a failed logout.
    subprocess.run(["swaymsg", "exit"], capture_output=True, timeout=10)
    assert session.wait(timeout=20) == 0
    session = None
    time.sleep(1)


def launch_from_drawer(name, app_id, pointer=False):
    key("d", ("logo",))
    wait("drawer shown", lambda: (RUNTIME / "ctlstdrawer.visible").exists())
    run("wtype", "-d", "60", name)
    time.sleep(1)
    screenshot(f"drawer-{app_id}")
    if pointer:
        pointer_at(90, 157)
        run("wlrctl", "pointer", "click")
    else:
        key("Return")
    wait(f"{app_id} window", lambda: app(app_id))
    wait("drawer dismissed after launch", lambda: not (RUNTIME / "ctlstdrawer.visible").exists())
    passed(f"drawer {'pointer' if pointer else 'keyboard'} launch {app_id}")


def check_home_edit_bottom_recovery():
    """The default 720x1080 Home fixture leaves its lower grid rows empty."""
    editing = RUNTIME / "ctlsthome.layout-editing"
    layout = CONFIG / "home-layout.json"

    def touch(x1, y1, x2, y2, duration):
        prefix = [] if os.access("/dev/uinput", os.W_OK) else ["sudo", "-S"]
        run(*prefix, "/tmp/ctlst-functional/touch-inject",
            "720", "1080", "3500", str(duration),
            str(x1), str(y1), str(x2), str(y2),
            input=os.environ.get("CTLST_CLEAN_VM_PASSWORD", "1111") + "\n")

    # Establish the saved baseline through the normal keyboard Home action.
    touch(360, 950, 360, 950, 650)
    wait("Home editor entered", editing.exists)
    key("Home", ("logo",))
    wait("keyboard Home leaves editor", lambda: not editing.exists())
    baseline = json.loads(layout.read_text())
    for phase in ("P", "R"):
        touch(360, 950, 360, 950, 650)
        wait("Home editor reentered", editing.exists)
        # Replay parsed digitizer events through the real gesture daemon.
        # This isolates motion/release routing from test-device hotplug.
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as client:
            client.sendto(b"e D 1 360 1078 360 1078 720 1080",
                          str(RUNTIME / "ctlst-gestures.sock"))
            client.sendto(f"e {phase} 1 360 1078 360 720 720 1080".encode(),
                          str(RUNTIME / "ctlst-gestures.sock"))
            if phase == "P":
                client.sendto(b"e R 1 360 1078 360 720 720 1080",
                              str(RUNTIME / "ctlst-gestures.sock"))
        wait("bottom Home leaves editor", lambda: not editing.exists())
        assert workspace() == "1"
        assert not (RUNTIME / "ctlstoverview.visible").exists()
        assert not (RUNTIME / "ctlstdrawer.visible").exists()
        assert json.loads(layout.read_text()) == baseline
    passed("bottom gesture motion/release replay exits editing without changing layout")


def main():
    user_config = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    normal_sway = user_config / "sway/config"
    normal_sway.parent.mkdir(parents=True, exist_ok=True)
    if not normal_sway.exists():
        normal_sway.write_text("# existing ordinary Sway config\nset $mod Mod1\n")
    sentinel = normal_sway.read_bytes()
    assert not (Path.home() / ".local/share/sway-touch/scripts").exists()
    desktop_dir = Path.home() / ".local/share/applications"
    desktop_dir.mkdir(parents=True, exist_ok=True)
    for suffix in ("one", "two"):
        (desktop_dir / f"ctlst-test-{suffix}.desktop").write_text(
            f"[Desktop Entry]\nType=Application\nName=CTLST Test {suffix}\n"
            f"Exec=foot --app-id=ctlst-test-{suffix} --title=CTLST-Test-{suffix} sh -c 'sleep 900'\n"
            "Icon=utilities-terminal\nTerminal=false\n")
    entry = configparser.ConfigParser()
    entry.read("/usr/share/wayland-sessions/ctlst.desktop")
    assert entry["Desktop Entry"]["Exec"] == "ctlst-session start"
    run("desktop-file-validate", "/usr/share/applications/dev.ctlst.Settings.desktop")
    start_session(1)
    assert not (user_config / "ctlst/session.conf").exists()
    assert not (user_config / "ctlst/sway.conf").exists()
    screenshot("home")
    assert wallpaper_state()["id"].startswith("packaged:")
    assert pixel("home", 680, 950) != (16, 21, 19), "default wallpaper was not rendered"
    passed("fresh installed session, visible status bar and packaged wallpaper, nine live endpoints")
    launch_from_drawer("CTLST Test one", "ctlst-test-one")
    first_workspace = workspace()
    launch_from_drawer("CTLST Test two", "ctlst-test-two")
    second_workspace = workspace()
    assert first_workspace != second_workspace
    key("Tab", ("logo", "shift"))
    wait("previous task via keyboard", lambda: workspace() == first_workspace)
    key("Tab", ("logo",))
    wait("next task via keyboard", lambda: workspace() == second_workspace)
    passed("keyboard task navigation")
    time.sleep(1.2)
    pointer_at(360, 1070)
    run("wlrctl", "pointer", "scroll", "-120", "0")
    wait("task changed with wheel", lambda: workspace() != second_workspace)
    passed("pointer wheel over collapsed charm")
    run("sudo", "-S", "/tmp/ctlst-functional/touch-inject", "720", "1080", "3500", "200",
        "360", "1078", "360", "720", input=os.environ.get("CTLST_CLEAN_VM_PASSWORD", "1111") + "\n")
    wait("touch flick Home", lambda: workspace() == "1")
    passed("hotplug-discovered touchscreen flick Home")
    check_home_edit_bottom_recovery()
    key("Tab", ("logo",))
    key("o", ("logo",))
    wait("overview shown", lambda: (RUNTIME / "ctlstoverview.visible").exists())
    screenshot("overview")
    key("BackSpace", ("logo",))
    wait("overview hidden", lambda: not (RUNTIME / "ctlstoverview.visible").exists())
    key("a", ("logo",))
    wait("shade shown", lambda: (RUNTIME / "ctlstshade.visible").exists())
    screenshot("shade")
    if not os.access(LIBEXEC / "sound-profile", os.X_OK):
        # A generic core install must explain its absent optional provider.
        # Open the real detail control, not private helper/test-only IPC.
        ui_button("Sound details")
        wait("missing sound provider explanation", lambda: any(
            node.get_name() == "Sound profiles unavailable"
            for node in accessible_nodes()))
        (ARTIFACTS / "shade-accessibility.json").write_text(json.dumps([
            {"name": node.get_name(), "role": node.get_role_name(),
             "sensitive": node.get_state_set().contains(Atspi.StateType.SENSITIVE)}
            for node in accessible_nodes()], indent=2))
        def disabled_profile():
            for node in accessible_nodes():
                if "Vibrate" not in node.get_name():
                    continue
                # GTK may expose the text on a child label rather than name
                # its containing button. Check the actionable ancestor itself.
                for _ in range(4):
                    if node is None:
                        break
                    if node.get_role() == Atspi.Role.PUSH_BUTTON:
                        # GTK exposes each widget's local sensitivity through
                        # AT-SPI; the grid disables all three children through
                        # inherited sensitivity. Do not inspect application or
                        # desktop states (those are always insensitive).
                        for _ in range(4):
                            if node is None or node.get_role() in (
                                    Atspi.Role.FRAME, Atspi.Role.APPLICATION):
                                break
                            if not node.get_state_set().contains(Atspi.StateType.SENSITIVE):
                                return True
                            node = node.get_parent()
                        return False
                    node = node.get_parent()
            return False
        wait("unavailable profile is insensitive", disabled_profile)
        screenshot("shade-sound-unavailable")
        passed("missing sound provider is explained and profile controls are disabled")
    key("BackSpace", ("logo",))
    wait("shade hidden", lambda: not (RUNTIME / "ctlstshade.visible").exists())
    passed("overview and shade keyboard routes")
    key("Escape", ("logo",))
    wait("lock shown", lambda: (RUNTIME / "ctlstlock.visible").exists())
    screenshot("lock")
    key("Return")
    wait("lock released", lambda: not (RUNTIME / "ctlstlock.visible").exists())
    passed("keyboard lock surface and unlock")
    (ARTIFACTS / "settings-ready.log").unlink(missing_ok=True)
    launch_from_drawer("Settings", "dev.ctlst.Settings", pointer=True)
    wait("Settings category index ready", lambda: (ARTIFACTS / "settings-ready.log").is_file()
         and "settings index ready" in (ARTIFACTS / "settings-ready.log").read_text())
    passed("Settings category index ready without optional phone helpers")
    focused_name = workspace()
    focused_tree = next(n for n in nodes(sway("get_tree")) if n.get("type") == "workspace" and n.get("name") == focused_name)
    assert [n["app_id"] for n in nodes(focused_tree) if n.get("app_id")] == ["dev.ctlst.Settings"]
    passed("slow-starting Settings keeps its own task workspace")
    screenshot("settings")
    # Exercise real Settings controls, not a test-only application API.
    ui_button("Wallpaper")
    ui_button("Apply wallpaper")
    # The packaged wallpaper is already selected on a fresh install. Checking
    # its ID alone can return before the queued Apply action even starts,
    # racing the next click against the busy state. Await actual UI completion.
    wait("Settings finished saving wallpaper", lambda: any(
        node.get_name() == "Wallpaper saved" for node in accessible_nodes()))
    wait("packaged wallpaper selected", lambda: wallpaper_state()["id"].startswith("packaged:"))
    assert not Path("/usr/bin/ctlst-files").exists(), "optional file app unexpectedly installed"
    imported_source = Path.home() / "test wallpaper import.png"
    fixture = GdkPixbuf.Pixbuf.new(GdkPixbuf.Colorspace.RGB, False, 8, 720, 1080)
    fixture.fill(0xCC3366FF)
    fixture.savev(str(imported_source), "png", [], [])
    ui_button("Choose from files")
    wait("native wallpaper chooser opened", lambda: any(
        node.get_name() == "Choose wallpaper" for node in accessible_nodes()))
    screenshot("wallpaper-file-chooser")
    key("l", ("ctrl",))
    run("wtype", str(imported_source))
    time.sleep(0.3)
    ui_button("Open")
    wait("native wallpaper chooser closed", lambda: not any(
        node.get_name() == "Choose wallpaper" for node in accessible_nodes()))
    # File selection is only a draft; import is an explicit Apply action.
    ui_button("Apply wallpaper")
    wait("file chooser imported wallpaper", lambda: wallpaper_state()["id"].startswith("managed:"))
    imported = wallpaper_state()
    imported_source.unlink()
    key("Home", ("logo",))
    screenshot("home-imported-wallpaper")
    assert pixel("home-imported-wallpaper", 680, 950) == (204, 51, 102)
    passed("Settings packaged wallpaper and native file import visibly apply without optional apps")
    assert subprocess.run([str(LIBEXEC / "ctlst-workspaced"), "prepare-launch", "dev.ctlst.Settings.desktop"],
                          capture_output=True).returncode in (0, 10)
    screenshot("settings-before-theme")
    select_theme_in_settings("paper")
    screenshot("settings-after-theme")
    assert pixel("settings-before-theme", 20, 210) != pixel("settings-after-theme", 20, 210), "Settings did not reload its theme"
    key("Home", ("logo",))
    screenshot("home-paper-theme")
    assert pixel("home-paper-theme", 680, 950) == (204, 51, 102), "theme reset the wallpaper"
    key("d", ("logo",))
    screenshot("drawer-paper-theme")
    key("Escape")
    passed("theme reload visibly updates Settings and preserves chosen wallpaper")
    run("swaymsg", '[app_id="dev.ctlst.Settings"] kill')
    config = user_config / "ctlst"
    config.mkdir(exist_ok=True)
    sway_override = "gaps inner 17\nbindsym Mod4+F12 workspace number 17\n"
    (config / "sway.conf").write_text(sway_override)
    (config / "input.conf").write_text("[input]\nkeyboard_repeat_rate_hz = 23\n")
    (config / "theme.conf").write_text("[theme]\nname = paper\naccent = cc6655\n")
    run("ctlst-session", "reload")
    wait("theme generated", lambda: "cc6655" in (RUNTIME / "ctlst-shell/generated/theme.env").read_text())
    key("F12", ("logo",))
    wait("user Sway binding", lambda: workspace() == "17")
    key("Home", ("logo",))
    wait("home via keyboard", lambda: workspace() == "1")
    screenshot("home-user-theme")
    assert pixel("home-user-theme", 680, 950) == (204, 51, 102)
    assert "repeat_rate 23" in (RUNTIME / "ctlst-shell/generated/input.conf").read_text()
    assert any(device.get("repeat_rate") == 23 for device in sway("get_inputs") if device.get("type") == "keyboard")
    passed("user Sway, input and theme dotfiles reload")
    plan = json.loads(run(str(LIBEXEC / "app-run"), "--explain", "command", "test", "true"))
    assert set(plan["limits"].values()) == {0}
    (config / "memory.conf").write_text("[app]\nmemory_high_bytes = 67108864\nmemory_max_bytes = 134217728\n")
    plan = json.loads(run(str(LIBEXEC / "app-run"), "--explain", "command", "test", "true"))
    assert plan["limits"]["MemoryMax"] == 134217728
    limited = subprocess.Popen([str(LIBEXEC / "app-run"), "command-new", "memory-probe", "sleep", "60"],
                               stdout=subprocess.DEVNULL, stderr=(ARTIFACTS / "memory-scope.log").open("w"))
    unit = f"ctlst-app-memory-probe-{limited.pid}.scope"
    try:
        def limited_cgroup():
            result = subprocess.run(["systemctl", "--user", "show", unit, "--property=ControlGroup", "--value"],
                                    text=True, capture_output=True)
            path = Path("/sys/fs/cgroup") / result.stdout.strip().lstrip("/")
            return path if (path / "memory.max").is_file() and result.stdout.strip() else None
        group = wait("limited application cgroup", limited_cgroup)
        assert (group / "memory.max").read_text().strip() == "134217728"
        assert (group / "memory.high").read_text().strip() == "67108864"
        passed("memory dotfile applies actual kernel cgroup limits")
    finally:
        subprocess.run(["systemctl", "--user", "stop", unit], capture_output=True)
        limited.wait(timeout=10)
    (config / "memory.conf").unlink()
    run("swaymsg", '[app_id="ctlst-test-one"] kill')
    run("swaymsg", '[app_id="ctlst-test-two"] kill')
    stop_session()
    # A new login must create live services, not inherit orphaned previous ones.
    start_session(2)
    screenshot("home-second-login")
    assert wallpaper_state() == imported
    assert pixel("home-second-login", 680, 950) == (204, 51, 102)
    assert run(str(LIBEXEC / "ctlst-theme"), "current").strip() == "paper"
    passed("wallpaper, theme and visible bar survive a new login")
    launch_from_drawer("CTLST Test one", "ctlst-test-one")
    screenshot("second-login")
    assert normal_sway.read_bytes() == sentinel
    assert (config / "sway.conf").read_text() == sway_override
    passed("logout/reentry retains user settings and ordinary Sway config")
    run("swaymsg", '[app_id="ctlst-test-one"] kill')
    stop_session()
    return 0


def cleanup():
    if session and session.poll() is None:
        session.terminate()
        try:
            session.wait(timeout=15)
        except subprocess.TimeoutExpired:
            session.kill()
    for name, original in original_dotfiles.items():
        if original is None:
            (CONFIG / name).unlink(missing_ok=True)
        else:
            (CONFIG / name).write_bytes(original)


if __name__ == "__main__":
    try:
        main()
    except Exception:
        if os.environ.get("SWAYSOCK"):
            try:
                screenshot("failure")
            except Exception:
                pass
        raise
    finally:
        (ARTIFACTS / "results.json").write_text(json.dumps({"passed": results}, indent=2) + "\n")
        cleanup()
