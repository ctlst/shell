#!/usr/bin/env python3
"""Native close/keep/force regression inside the isolated Sway VM harness."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
base = Path(tempfile.mkdtemp(prefix="ctlst-close-ui-"))
os.environ["SWAYSOCK"] = str(next(Path(os.environ["XDG_RUNTIME_DIR"]).glob("sway-ipc.*.sock")))
os.environ["XDG_CONFIG_HOME"] = str(base / "config")
(base / "config/ctlst").mkdir(parents=True)
(base / "config/ctlst/close.conf").write_text("[close]\ngrace_seconds=1\nconfirmation_seconds=10\n")
sys.path.insert(0, str(ROOT / "scripts"))
from ctlst_close import IPC, target
ipc = IPC(os.environ["SWAYSOCK"])
children = []

def wait(predicate, seconds=5):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(.05)
    raise AssertionError("wait timeout")

def spawn(app_id, refuse=False):
    proc = subprocess.Popen([sys.executable, str(ROOT / "tests/close-fixture.py"), app_id] + (["--refuse"] if refuse else []))
    children.append(proc)
    def find():
        n = target(ipc.request(4))
        return n if n and n["pid"] == proc.pid else None
    return proc, wait(find)

def helper(chosen):
    proc = subprocess.Popen([sys.executable, str(ROOT / "scripts/ctlst_close.py"), "--node", str(chosen["id"])])
    children.append(proc)
    return proc

def menu_present(proc):
    result = subprocess.run(["pgrep", "-P", str(proc.pid), "-x", "fuzzel"], capture_output=True, text=True)
    return bool(result.stdout.strip())

try:
    control, _ = spawn("dev.ctlst.CloseControl")
    hung, chosen = spawn("dev.ctlst.CloseFixture", True)
    close = helper(chosen)
    wait(lambda: menu_present(close))
    time.sleep(.25)
    subprocess.run(["grim", str(base / "confirmation.png")], check=True)
    subprocess.run(["wtype", "-k", "Return"], check=True)  # Keep open is first/default.
    assert close.wait(timeout=5) == 0
    assert hung.poll() is None and control.poll() is None
    close = helper(chosen)
    wait(lambda: menu_present(close))
    time.sleep(.25)
    subprocess.run(["wtype", "-k", "Down", "-k", "Return"], check=True)
    assert close.wait(timeout=5) == 0
    assert hung.wait(timeout=5) == -9
    assert control.poll() is None
    healthy, healthy_node = spawn("dev.ctlst.CloseHealthy")
    close = helper(healthy_node)
    assert close.wait(timeout=5) == 0
    assert healthy.wait(timeout=5) == 0
    assert control.poll() is None
    print("PASS: normal close, default Keep open, explicit Force quit, unrelated app survives")
    print("Evidence:", base)
finally:
    for proc in children:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=2)
