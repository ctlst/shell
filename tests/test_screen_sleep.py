"""Opt-in real Wayland idle/resume regression, run in the ARM shell VM.

CTLST_TEST_SCREEN_SLEEP=1 python -m pytest tests/test_screen_sleep.py
Uses a private headless Sway seat and mocked panel/lock hardware only.
"""
import os
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import time

import pytest

ROOT = Path(__file__).resolve().parents[1]
pytestmark = pytest.mark.skipif(
    os.environ.get("CTLST_TEST_SCREEN_SLEEP") != "1",
    reason="requires the Linux VM's headless Sway and swayidle",
)


def wait_for(check, timeout=8):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if check():
            return
        time.sleep(.03)
    raise AssertionError("Timed out waiting for screen state")


@pytest.fixture
def screen(tmp_path, request):
    home = tmp_path / "home"
    runtime = tmp_path / "runtime"
    scripts = tmp_path / "libexec"
    config = home / ".config/ctlst"
    binary = tmp_path / "bin"
    for path in (runtime, scripts, config, binary):
        path.mkdir(parents=True)
    runtime.chmod(0o700)
    for name in ("screen-idle",):
        shutil.copy2(ROOT / "scripts" / name, scripts / name)
    panel = tmp_path / "panel"
    panel.write_text("on")
    for name, body in {
        "swaymsg": '''#!/usr/bin/env python3
import json, os, pathlib, sys
p = pathlib.Path(os.environ['TEST_PANEL'])
if 'get_outputs' in sys.argv:
    print(json.dumps([{'name': 'DSI-1', 'power': p.read_text() == 'on'}]))
elif sys.argv[1:4] == ['output', os.environ.get('CTLST_OUTPUT_NAME', '*'), 'power']:
    p.write_text(sys.argv[4])
''',
    }.items():
        target = binary / name
        target.write_text(body)
        target.chmod(0o755)
    lock = scripts / "ctlstlock"
    lock.write_text('#!/bin/sh\n[ ! -e "$XDG_RUNTIME_DIR/fail-lock" ] || exit 1\ntouch "$XDG_RUNTIME_DIR/lock-started"\n[ ! -e "$XDG_RUNTIME_DIR/slow-lock" ] || sleep 1\ntouch "$XDG_RUNTIME_DIR/ctlstlock.visible"\n')
    lock.chmod(0o755)
    env = dict(os.environ, HOME=str(home), XDG_RUNTIME_DIR=str(runtime),
               WLR_BACKENDS="headless", WLR_LIBINPUT_NO_DEVICES="1",
               WLR_RENDERER="pixman", TEST_PANEL=str(panel),
               CTLST_LIBEXEC_DIR=str(scripts), CTLST_CONFIG_DIR=str(config))
    env.pop("CTLST_OUTPUT_NAME", None)
    if getattr(request, "param", None):
        env["CTLST_OUTPUT_NAME"] = request.param
    sway_config = tmp_path / "sway.conf"
    sway_config.write_text("output * mode 480x960\nseat seat0 fallback true\n")
    log = (tmp_path / "sway.log").open("w")
    sway = subprocess.Popen(["sway", "-c", str(sway_config)], env=env, stdout=log, stderr=log)
    try:
        wait_for(lambda: list(runtime.glob("wayland-*.lock")))
        env["WAYLAND_DISPLAY"] = next(runtime.glob("wayland-*.lock")).name.removesuffix(".lock")
        env["PATH"] = str(binary) + ":" + env["PATH"]

        def run(*args):
            return subprocess.run([str(scripts / args[0]), *args[1:]], env=env,
                                  capture_output=True, text=True, timeout=10, check=True)

        def activity():
            subprocess.run(["wtype", "-k", "Shift_L"], env=env, check=True)

        yield run, panel, runtime, activity
    finally:
        subprocess.run([str(scripts / "screen-idle"), "stop"], env=env, timeout=10)
        sway.terminate()
        sway.wait(timeout=10)
        log.close()


@pytest.mark.parametrize("timeout,inhibited", [(60, False), (0, False), (60, True)])
def test_manual_sleep_and_input_wake(screen, timeout, inhibited):
    run, panel, runtime, activity = screen
    run("screen-idle", "set", str(timeout))
    if inhibited:
        run("screen-idle", "inhibit", "on", "test app")
    for _ in range(2):
        run("screen-idle", "sleep")
        wait_for(lambda: panel.read_text() == "off")
        assert (runtime / "ctlstlock.visible").exists()
        activity()
        wait_for(lambda: panel.read_text() == "on")
        assert (runtime / "ctlstlock.visible").exists(), "Wake must not unlock"
    run("screen-idle", "sleep")
    wait_for(lambda: panel.read_text() == "off")
    run("screen-idle", "wake")
    activity()
    time.sleep(.7)
    assert panel.read_text() == "on"


def test_auto_sleep_and_manual_sleep_share_resume(screen):
    run, panel, runtime, activity = screen
    run("screen-idle", "set", "60")
    activity()
    wait_for(lambda: panel.read_text() == "off", timeout=65)
    activity()
    wait_for(lambda: panel.read_text() == "on")
    run("screen-idle", "sleep")
    wait_for(lambda: panel.read_text() == "off")
    activity()
    wait_for(lambda: panel.read_text() == "on")


def test_manual_sleep_recovers_missing_daemon_and_keeps_never(screen):
    run, panel, runtime, activity = screen
    run("screen-idle", "set", "0")
    run("screen-idle", "stop")
    run("screen-idle", "sleep")
    wait_for(lambda: panel.read_text() == "off")
    activity()
    wait_for(lambda: panel.read_text() == "on")
    assert run("screen-idle", "status").stdout.strip() == "Screen timeout off"


def test_failed_lock_does_not_blank_session(screen):
    run, panel, runtime, activity = screen
    (runtime / "fail-lock").touch()
    with pytest.raises(subprocess.CalledProcessError):
        run("screen-idle", "sleep")
    time.sleep(.5)
    assert panel.read_text() == "on"


def test_input_during_lock_preparation_does_not_wake_newly_blanked_panel(screen):
    run, panel, runtime, activity = screen
    run("screen-idle", "set", "0")
    (runtime / "slow-lock").touch()
    with ThreadPoolExecutor(max_workers=1) as pool:
        sleeping = pool.submit(run, "screen-idle", "sleep")
        wait_for(lambda: (runtime / "lock-started").exists())
        activity()  # trailing power/touch event while the lock maps
        sleeping.result(timeout=8)
    wait_for(lambda: panel.read_text() == "off")
    time.sleep(.5)
    assert panel.read_text() == "off", "Queued lock-transition input must not wake"
    activity()
    wait_for(lambda: panel.read_text() == "on")


@pytest.mark.parametrize("screen", [None, "DP-9"], indirect=True)
def test_default_and_configured_output(screen):
    run, panel, runtime, activity = screen
    run("screen-idle", "sleep")
    wait_for(lambda: panel.read_text() == "off")
    activity()
    wait_for(lambda: panel.read_text() == "on")
