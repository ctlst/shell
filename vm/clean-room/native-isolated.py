"""Run bounded native GTK regressions in their own disposable Sway session."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import time

p = argparse.ArgumentParser()
p.add_argument("--disposable-vm", required=True, action="store_true")
p.add_argument("binary", nargs="+")
a = p.parse_args()
base = Path(tempfile.mkdtemp(prefix="ctlst-native-isolated-"))
runtime = base / "runtime"
runtime.mkdir(mode=0o700)
env = os.environ | {"XDG_RUNTIME_DIR": str(runtime), "WLR_BACKENDS": "headless",
    "WLR_RENDERER": "pixman", "GSK_RENDERER": "cairo", "GTK_A11Y": "none",
    "XDG_CONFIG_HOME": str(base / "config"), "XDG_CACHE_HOME": str(base / "cache")}
env.pop("SWAYSOCK", None)
env.pop("WAYLAND_DISPLAY", None)
config = base / "sway.conf"
config.write_text("output HEADLESS-1 mode 960x1920 scale 2\nxwayland disable\n")
with (base / "sway.log").open("w") as log:
    sway = subprocess.Popen(["sway", "-c", str(config)], env=env, stdout=log, stderr=log)
try:
    deadline = time.monotonic() + 8
    while not list(runtime.glob("wayland-*.lock")):
        assert sway.poll() is None, (base / "sway.log").read_text()
        assert time.monotonic() < deadline, "Sway startup timeout"
        time.sleep(.05)
    env["WAYLAND_DISPLAY"] = next(runtime.glob("wayland-*.lock")).name.removesuffix(".lock")
    for binary in a.binary:
        result = subprocess.run([str(Path(binary).resolve())], env=env,
            text=True, capture_output=True, timeout=30)
        (base / (Path(binary).name + ".log")).write_text(result.stdout + result.stderr)
        print(result.stdout + result.stderr, flush=True)
        assert result.returncode == 0, f"{binary}: exit {result.returncode}"
finally:
    sway.terminate()
    sway.wait(timeout=5)
    print("Evidence:", base, flush=True)
