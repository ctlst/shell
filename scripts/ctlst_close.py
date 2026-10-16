"""Close a captured Sway window; explicit, identity-pinned force-quit fallback."""
import argparse
import configparser
import fcntl
import json
import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import sys
import time

KEEP = "Keep open"
FORCE = "Force quit (unsaved work lost)"
PROTECTED = {"sway", "Xwayland", "waybar", "mako", "systemd", "dbus-broker",
             "dbus-daemon", "sshd", "pipewire", "wireplumber", "pulseaudio",
             "ctlst-gestured", "ctlsthome", "ctlstshade", "ctlstdock", "ctlstaction",
             "ctlstlock", "ctlstoverview", "ctlstmessages", "ctlstdialer",
             "wvkbd-swaytouch", "wvkbd-mobintl"}
PROTECTED_APPS = {"dev.ctlst.Home", "dev.ctlst.Shade", "dev.ctlst.Dock",
                  "dev.ctlst.Lock", "dev.ctlst.Overview", "dev.ctlst.Dialer",
                  "dev.ctlst.Messages", "dev.ctlst.ClosePrompt"}


def preferences():
    parser = configparser.ConfigParser(interpolation=None)
    root = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    parser.read(root / "ctlst/close.conf")
    grace = parser.getfloat("close", "grace_seconds", fallback=3)
    timeout = parser.getfloat("close", "confirmation_seconds", fallback=45)
    if not 1 <= grace <= 15 or not 5 <= timeout <= 120:
        raise ValueError("close.conf: grace_seconds must be 1–15 and confirmation_seconds 5–120")
    return grace, timeout


class IPC:
    def __init__(self, path):
        self.path = path

    def connect(self):
        sock = socket.socket(socket.AF_UNIX)
        sock.settimeout(2)
        sock.connect(self.path)
        return sock

    @staticmethod
    def receive(sock):
        def exact(size):
            data = b""
            while len(data) < size:
                part = sock.recv(size - len(data))
                if not part:
                    raise OSError("Sway IPC closed")
                data += part
            return data
        header = exact(14)
        length, kind = struct.unpack("=II", header[6:])
        if header[:6] != b"i3-ipc" or length > 16 * 1024 * 1024:
            raise ValueError("Invalid Sway reply")
        return kind, json.loads(exact(length))

    @staticmethod
    def send(sock, kind, payload):
        data = payload.encode()
        sock.sendall(b"i3-ipc" + struct.pack("=II", len(data), kind) + data)

    def request(self, kind, payload=""):
        with self.connect() as sock:
            self.send(sock, kind, payload)
            reply_kind, reply = self.receive(sock)
            if reply_kind != kind:
                raise ValueError("Unexpected Sway reply")
            if kind == 0 and not all(item.get("success") for item in reply):
                raise RuntimeError("Sway could not close the selected window")
            return reply

    def subscribe(self):
        sock = self.connect()
        try:
            self.send(sock, 2, '["window","shutdown"]')
            _, reply = self.receive(sock)
            if not reply.get("success"):
                raise RuntimeError("Cannot watch selected window")
            return sock
        except Exception:
            sock.close()
            raise


def target(tree, node_id=None, workspace=None):
    if tree.get("type") == "workspace":
        workspace = tree.get("name")
    if tree.get("pid") and (tree.get("id") == node_id if node_id is not None else tree.get("focused")):
        return {"id": tree["id"], "pid": tree["pid"], "workspace": workspace,
                "app": tree.get("app_id") or (tree.get("window_properties") or {}).get("class", "Application"),
                "visible": tree.get("visible", False)}
    for child in tree.get("nodes", []) + tree.get("floating_nodes", []):
        match = target(child, node_id, workspace)
        if match:
            return match
    return None


def same_target(original, current):
    return current is not None and all(current.get(k) == original.get(k) for k in ("id", "pid", "app"))


def process_identity(pid):
    path = Path("/proc") / str(pid)
    fields = (path / "stat").read_text().rsplit(")", 1)[1].split()
    return (path.stat().st_uid, int(fields[19]), (path / "comm").read_text().strip(),
            Path(os.readlink(path / "exe")).name)


class PinnedProcess:
    def __init__(self, chosen):
        self.fd = None
        self.identity = None
        self.chosen = chosen
        pid = chosen["pid"]
        if pid <= 1 or pid == os.getpid() or chosen["app"] in PROTECTED_APPS:
            return
        try:
            identity = process_identity(pid)
            if identity[0] != os.getuid() or identity[2] in PROTECTED or identity[3] in PROTECTED:
                return
            if not hasattr(signal, "pidfd_send_signal"):
                return
            self.fd = os.pidfd_open(pid)
            self.identity = identity
            if process_identity(pid) != identity:
                self.close()
        except (OSError, ValueError, IndexError, AttributeError):
            self.close()

    def alive(self):
        return self.fd is not None and not select.select([self.fd], [], [], 0)[0]

    def force(self, current):
        if not same_target(self.chosen, current) or not self.alive():
            return False
        try:
            if process_identity(self.chosen["pid"]) != self.identity:
                return False
        except (OSError, ValueError, IndexError):
            return False
        signal.pidfd_send_signal(self.fd, signal.SIGKILL)
        return True

    def close(self):
        if self.fd is not None:
            os.close(self.fd)
            self.fd = None


def blocked(runtime):
    return (runtime / "ctlstlock.visible").exists()


def event_cancels(kind, event, chosen):
    if kind == 0x80000006:  # compositor shutdown
        return True
    node = event.get("container", {})
    return (event.get("change") == "close" and node.get("id") == chosen["id"] or
            event.get("change") == "focus" and node.get("pid") not in (None, chosen["pid"]))


def watch(sock, chosen, seconds, runtime, menu=None):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if blocked(runtime):
            return False
        if menu and menu.poll() is not None:
            return True
        if select.select([sock], [], [], min(.25, max(0, deadline - time.monotonic())))[0]:
            kind, event = IPC.receive(sock)
            if event_cancels(kind, event, chosen):
                return False
    return menu is None  # A timed-out confirmation always cancels.


def confirm(sock, chosen, runtime, seconds):
    app = "".join(c for c in chosen["app"] if c.isprintable())[:24] or "Application"
    command = ["fuzzel", "--dmenu", f"--prompt={app} still open: ", "--only-match", "--no-sort",
               "--width=28", "--font=Noto Sans:size=14", "--line-height=44",
               "--lines=2", "--minimal-lines", "--selection-radius=12"]
    generated = Path(os.environ.get("CTLST_GENERATED_DIR", Path.home() / ".config/ctlst"))
    for path in (generated / "fuzzel.ini", Path.home() / ".config/sway-touch/session-menu.ini",
                 Path.home() / ".config/sway-touch/fuzzel.ini"):
        if path.is_file():
            command.append(f"--config={path}")
            break
    # The menu is independent of the stuck application's event loop and scope.
    menu = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.DEVNULL, text=True)
    try:
        menu.stdin.write(KEEP + "\n" + FORCE + "\n")
        menu.stdin.close()
        if not watch(sock, chosen, seconds, runtime, menu):
            return False
        return menu.returncode == 0 and menu.stdout.read(256).strip() == FORCE
    finally:
        if menu.poll() is None:
            menu.terminate()
            try:
                menu.wait(timeout=2)
            except subprocess.TimeoutExpired:
                menu.kill()
                menu.wait(timeout=2)
        menu.stdout.close()


def close_window(ipc, runtime, node_id=None, return_home=False):
    if blocked(runtime):
        return
    grace, timeout = preferences()
    chosen = target(ipc.request(4), node_id)
    if not chosen:
        return
    pinned = PinnedProcess(chosen)
    try:
        with ipc.subscribe() as events:
            ipc.request(0, f"[con_id={int(chosen['id'])}] kill")
            remained = watch(events, chosen, grace, runtime)
            tree = ipc.request(4)
            current = target(tree, chosen["id"])
            focused = target(tree)
            if (remained and same_target(chosen, current) and current["visible"]
                    and focused and focused["pid"] == chosen["pid"] and pinned.alive()
                    and confirm(events, chosen, runtime, timeout) and not blocked(runtime)):
                # Never re-resolve "focused": the confirmation belongs to this window/PID.
                pinned.force(target(ipc.request(4), chosen["id"]))
            elif not current and return_home:
                workspaces = ipc.request(1)
                if any(w.get("focused") and w.get("name") == chosen["workspace"] for w in workspaces):
                    ipc.request(0, "workspace number 1")
    finally:
        pinned.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--node", type=int)
    parser.add_argument("--return-home", action="store_true")
    args = parser.parse_args()
    if args.node is not None and args.node <= 0:
        parser.error("--node must be positive")
    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
    fd = os.open(runtime / "ctlst-close.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, "w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
        close_window(IPC(os.environ["SWAYSOCK"]), runtime, args.node, args.return_home)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, RuntimeError, KeyError) as error:
        print(f"close-active: {error}", file=sys.stderr)
        raise SystemExit(1)
