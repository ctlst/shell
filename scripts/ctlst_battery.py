"""Local battery telemetry. Standard-library only; never writes hardware state."""
import configparser
import csv
import fcntl
import io
import json
import math
import os
from pathlib import Path
import signal
import socket
import sqlite3
import struct
import threading
import time


def state_dir():
    return Path(os.environ.get("XDG_STATE_HOME", Path.home() / ".local/state")) / "ctlst/battery"


def config_path():
    return Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config")) / "ctlst/battery.conf"


def settings(path=None):
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(path or config_path())
    if not parser.has_section("battery"):
        parser.add_section("battery")
    section = parser["battery"]
    result = {"interval_seconds": section.getint("interval_seconds", 60),
              "retention_days": section.getint("retention_days", 30),
              "process_activity": section.getboolean("process_activity", True),
              "device": section.get("device", "auto")}
    for key, low, high in (("interval_seconds", 30, 600), ("retention_days", 1, 90)):
        if not low <= result[key] <= high:
            raise ValueError(f"{key} must be between {low} and {high}")
    if not result["device"] or "/" in result["device"] or result["device"] in (".", ".."):
        raise ValueError("device must be auto or a power_supply name")
    return result


def read(path):
    try:
        return Path(path).read_text().strip()
    except OSError:
        return ""


def number(value, scale=1):
    try:
        result = float(value) / scale
        return result if math.isfinite(result) else None
    except (TypeError, ValueError):
        return None


def batteries(root=Path("/sys/class/power_supply")):
    return sorted(p for p in root.glob("*") if read(p / "type") == "Battery"
                  and read(p / "scope") != "Device")


def telemetry(device="auto", root=Path("/sys/class/power_supply")):
    available = batteries(root)
    chosen = next((p for p in available if device == "auto" or p.name == device), None)
    if chosen is None:
        return {"device": None, "status": "Unavailable", "capacity": None}
    props = dict(line.removeprefix("POWER_SUPPLY_").split("=", 1)
                 for line in read(chosen / "uevent").splitlines() if "=" in line)
    def prop(name):
        return props.get(name.upper(), read(chosen / name))
    result = {"device": chosen.name, "status": prop("status") or "Unknown",
              "health": prop("health") or None, "technology": prop("technology") or None,
              "battery_count": len(available)}
    for key, scale in (("capacity", 1), ("voltage_now", 1e6), ("current_now", 1e6),
                       ("power_now", 1e6), ("temp", 10), ("charge_full", 1000),
                       ("charge_full_design", 1000), ("energy_now", 1e6),
                       ("energy_full", 1e6), ("energy_full_design", 1e6),
                       ("charge_now", 1000), ("cycle_count", 1)):
        result[key] = number(prop(key), scale)
    if result["capacity"] is not None and not 0 <= result["capacity"] <= 100:
        result["capacity"] = None
    power = result["power_now"]
    if power is None and result["voltage_now"] is not None and result["current_now"] is not None:
        power = result["voltage_now"] * result["current_now"]
    # Drivers disagree on current polarity. Direction comes from STATUS, never sign.
    result["watts"] = abs(power) if power is not None and result["status"] in ("Charging", "Discharging") else None
    result["wear_percent"] = None
    for key in ("energy", "charge"):
        full, design = result[key + "_full"], result[key + "_full_design"]
        if full is not None and design and full > 0:
            result["wear_percent"] = full / design * 100
            break
    return result


def processes(root=Path("/proc")):
    result = {}
    ticks = os.sysconf("SC_CLK_TCK")
    for path in list(root.glob("[0-9]*"))[:8192]:
        try:
            value = (path / "stat").read_text()
            end = value.rindex(")")
            fields = value[end + 2:].split()
            name = value[value.index("(") + 1:end][:80]
            result[(int(path.name), int(fields[19]))] = (
                name, (int(fields[11]) + int(fields[12])) / ticks)
        except (OSError, ValueError, IndexError):
            continue
    return result


def process_delta(previous, current):
    totals = {}
    for identity, (name, seconds) in current.items():
        if identity in previous:
            delta = seconds - previous[identity][1]
            if delta > 0:
                totals[name] = totals.get(name, 0) + delta
    return sorted(totals.items(), key=lambda item: (-item[1], item[0]))[:32]


def screen_state():
    """One small GET_OUTPUTS reply per sample, never GET_TREE polling."""
    runtime = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
    paths = [os.environ["SWAYSOCK"]] if os.environ.get("SWAYSOCK") else sorted(runtime.glob("sway-ipc.*.sock"))
    for path in paths[:4]:
        try:
            with socket.socket(socket.AF_UNIX) as sock:
                sock.settimeout(0.25)
                sock.connect(str(path))
                sock.sendall(b"i3-ipc" + struct.pack("=II", 0, 3))
                def receive(count):
                    data = b""
                    while len(data) < count:
                        chunk = sock.recv(count - len(data))
                        if not chunk:
                            raise OSError("short Sway reply")
                        data += chunk
                    return data
                header = receive(14)
                size, kind = struct.unpack("=II", header[6:])
                if header[:6] != b"i3-ipc" or kind != 3 or size > 262144:
                    continue
                outputs = json.loads(receive(size))
                states = [o.get("power", o.get("dpms")) for o in outputs if o.get("active")]
                if states and all(isinstance(s, bool) for s in states):
                    return int(any(states))
        except (OSError, ValueError, TypeError, AttributeError):
            continue
    return None


def brightness(root=Path("/sys/class/backlight")):
    for path in sorted(root.glob("*")):
        maximum = number(read(path / "max_brightness"))
        actual = number(read(path / "actual_brightness"))
        if maximum and actual is not None:
            return actual / maximum * 100
    return None


class Store:
    def __init__(self, directory=None):
        self.directory = Path(directory) if directory else state_dir()
        self.directory.mkdir(parents=True, exist_ok=True, mode=0o700)
        self.directory.chmod(0o700)
        self.path = self.directory / "history.sqlite3"
        fd = os.open(self.path, os.O_CREAT | os.O_RDWR, 0o600)
        os.close(fd)
        self.path.chmod(0o600)
        self.db = sqlite3.connect(self.path, timeout=3)
        self.db.row_factory = sqlite3.Row
        version = self.db.execute("PRAGMA user_version").fetchone()[0]
        if version not in (0, 1):
            self.db.close()
            raise RuntimeError(f"Unsupported battery database version {version}; data preserved")
        self.db.execute("PRAGMA foreign_keys=ON")
        self.db.executescript("""
            CREATE TABLE IF NOT EXISTS samples (
                id INTEGER PRIMARY KEY, timestamp REAL NOT NULL, boot TEXT NOT NULL,
                elapsed REAL NOT NULL, awake REAL NOT NULL, interval REAL NOT NULL,
                device TEXT, status TEXT NOT NULL, capacity REAL, watts REAL, temp REAL,
                screen INTEGER, brightness REAL, payload TEXT NOT NULL);
            CREATE INDEX IF NOT EXISTS sample_time ON samples(timestamp);
            CREATE TABLE IF NOT EXISTS activity (
                sample INTEGER REFERENCES samples(id) ON DELETE CASCADE,
                name TEXT NOT NULL, cpu_seconds REAL NOT NULL);
            CREATE INDEX IF NOT EXISTS activity_sample ON activity(sample);
            CREATE TABLE IF NOT EXISTS events (
                timestamp REAL NOT NULL, device TEXT NOT NULL, kind TEXT NOT NULL, capacity REAL);
            CREATE INDEX IF NOT EXISTS event_time ON events(timestamp);
            CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            PRAGMA user_version=1;
        """)

    def append(self, sample, activity=()):
        with self.db:
            cursor = self.db.execute("""INSERT INTO samples
                (timestamp,boot,elapsed,awake,interval,device,status,capacity,watts,temp,screen,brightness,payload)
                VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?)""", tuple(sample.get(k) for k in
                ("timestamp", "boot", "elapsed", "awake", "interval", "device", "status", "capacity",
                 "watts", "temp", "screen", "brightness")) + (json.dumps(sample),))
            self.db.executemany("INSERT INTO activity VALUES (?,?,?)",
                                [(cursor.lastrowid, name, seconds) for name, seconds in activity])
            device = sample.get("device")
            if device:
                key = "status:" + device
                old = self.db.execute("SELECT value FROM meta WHERE key=?", (key,)).fetchone()
                if not old or old[0] != sample["status"]:
                    self.db.execute("INSERT INTO events VALUES (?,?,?,?)", (
                        sample["timestamp"], device, sample["status"], sample.get("capacity")))
                self.db.execute("INSERT OR REPLACE INTO meta VALUES (?,?)", (key, sample["status"]))
                # Last observed full is refreshed while held full. It is not a guessed charge end.
                if sample["status"] == "Full" or sample.get("capacity") == 100:
                    self.db.execute("INSERT OR REPLACE INTO meta VALUES (?,?)",
                                    ("full:" + device, str(sample["timestamp"])))

    def prune(self, cutoff):
        with self.db:
            self.db.execute("DELETE FROM samples WHERE timestamp < ?", (cutoff,))
            self.db.execute("DELETE FROM events WHERE timestamp < ?", (cutoff,))


def contiguous(a, b):
    delta = b["elapsed"] - a["elapsed"]
    wall = b["timestamp"] - a["timestamp"]
    return (a["boot"] == b["boot"] and a["device"] == b["device"]
            and 0 < delta <= max(a["interval"], b["interval"]) * 2.5
            and abs(wall - delta) < 5)


def summarize(samples, now):
    totals = {"covered_seconds": 0.0, "screen_on_seconds": 0.0, "screen_known_seconds": 0.0,
              "discharge_seconds": 0.0, "discharge_points": 0.0, "energy_wh": 0.0,
              "energy_seconds": 0.0, "screen_on_wh": 0.0, "screen_off_wh": 0.0,
              "screen_on_power_seconds": 0.0, "screen_off_power_seconds": 0.0}
    for a, b in zip(samples, samples[1:]):
        if not contiguous(a, b):
            continue
        dt = b["elapsed"] - a["elapsed"]
        totals["covered_seconds"] += dt
        if a["screen"] == b["screen"] and a["screen"] is not None:
            totals["screen_known_seconds"] += dt
            if a["screen"]:
                totals["screen_on_seconds"] += dt
        if a["status"] != "Discharging" or b["status"] != "Discharging":
            continue
        if a["capacity"] is not None and b["capacity"] is not None:
            totals["discharge_seconds"] += dt
            totals["discharge_points"] += a["capacity"] - b["capacity"]
        if a["watts"] is not None and b["watts"] is not None:
            wh = (a["watts"] + b["watts"]) / 2 * dt / 3600
            totals["energy_wh"] += wh
            totals["energy_seconds"] += dt
            if a["screen"] == b["screen"] and a["screen"] is not None:
                key = "screen_on" if a["screen"] else "screen_off"
                totals[key + "_wh"] += wh
                totals[key + "_power_seconds"] += dt
    totals["rate_per_hour"] = None
    if totals["discharge_seconds"] >= 900 and totals["discharge_points"] >= 1:
        totals["rate_per_hour"] = totals["discharge_points"] * 3600 / totals["discharge_seconds"]
    # ETA uses only the most recent uninterrupted discharge stretch, up to two hours.
    recent = []
    for row in reversed(samples):
        if row["status"] != "Discharging" or now - row["timestamp"] > 7200:
            break
        if recent and not contiguous(row, recent[-1]):
            break
        recent.append(row)
    totals["remaining_hours"] = None
    if len(recent) >= 2 and now - recent[0]["timestamp"] <= recent[0]["interval"] * 2.5:
        last, first = recent[0], recent[-1]
        if last["capacity"] is not None and first["capacity"] is not None:
            duration = last["elapsed"] - first["elapsed"]
            loss = first["capacity"] - last["capacity"]
            if duration >= 900 and loss >= 1:
                totals["remaining_hours"] = last["capacity"] * duration / loss / 3600
    return totals


def report(hours=24, directory=None, now=None):
    now = time.time() if now is None else now
    path = (Path(directory) if directory else state_dir()) / "history.sqlite3"
    if not path.exists():
        return {"samples": [], "events": [], "activity": [], "latest": None, "last_full": None,
                "summary": summarize([], now), "since": now - hours * 3600, "now": now}
    with sqlite3.connect(path.as_uri() + "?mode=ro", uri=True, timeout=3) as db:
        db.row_factory = sqlite3.Row
        db.execute("BEGIN")  # Keep latest, samples and activity on one read snapshot.
        latest = db.execute("SELECT * FROM samples ORDER BY id DESC LIMIT 1").fetchone()
        if latest is None:
            return {"samples": [], "events": [], "activity": [], "latest": None, "last_full": None,
                    "summary": summarize([], now), "since": now - hours * 3600, "now": now}
        full = db.execute("SELECT value FROM meta WHERE key=?", ("full:" + str(latest["device"]),)).fetchone()
        full = float(full[0]) if full else None
        since = max(now - 90 * 86400, full) if hours == 0 and full else now - (hours or 24) * 3600
        rows = [dict(r) for r in db.execute("SELECT id,timestamp,boot,elapsed,awake,interval,device,status,capacity,watts,temp,screen,brightness FROM samples WHERE timestamp>=? AND timestamp<=? AND device IS ? ORDER BY id", (since, now, latest["device"]))]
        summary = summarize(rows, now)
        # Aggregate in SQLite rather than transferring a month's process rows to GTK.
        # Mirror contiguous(): only adjacent same-battery discharging observations.
        activity = [tuple(r) for r in db.execute("""
            SELECT a.name, SUM(a.cpu_seconds) AS seconds FROM samples s
            JOIN samples p ON p.id = s.id - 1
            JOIN activity a ON a.sample = s.id
            WHERE s.timestamp >= ? AND s.timestamp <= ? AND p.timestamp >= ?
                AND s.device IS ? AND p.device = s.device AND p.boot = s.boot
                AND s.status = 'Discharging' AND p.status = 'Discharging'
                AND s.elapsed > p.elapsed
                AND s.elapsed - p.elapsed <= MAX(s.interval,p.interval) * 2.5
                AND ABS((s.timestamp-p.timestamp)-(s.elapsed-p.elapsed)) < 5
            GROUP BY a.name ORDER BY seconds DESC, a.name LIMIT 20
        """, (since, now, since, latest["device"]))]
        events = [dict(r) for r in db.execute("SELECT * FROM events WHERE timestamp>=? AND timestamp<=? AND device=? ORDER BY timestamp DESC LIMIT 100", (since, now, latest["device"]))]
        return {"samples": rows, "events": events,
                "activity": activity,
                "latest": json.loads(latest["payload"]), "last_full": full,
                "summary": summary, "since": since, "now": now}


def export_csv(data):
    buffer = io.StringIO()
    fields = ("timestamp", "device", "status", "capacity", "watts", "temp", "screen", "brightness")
    writer = csv.DictWriter(buffer, fieldnames=fields, extrasaction="ignore")
    writer.writeheader()
    writer.writerows(data["samples"])
    return buffer.getvalue()


def daemon(once=False):
    os.umask(0o077)
    config = settings()
    directory = state_dir()
    directory.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (directory / "recorder.lock").open("a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return  # Session autostart and optional user service share a recorder.
        store = Store()
        boot = read("/proc/sys/kernel/random/boot_id") or "unknown"
        previous, previous_sample = {}, None
        stopped = threading.Event()
        def stop(_sig, _frame):
            stopped.set()
        signal.signal(signal.SIGTERM, stop)
        signal.signal(signal.SIGINT, stop)
        last_prune = 0
        try:
            while not stopped.is_set():
                started = time.monotonic()
                sample = telemetry(config["device"])
                sample.update(timestamp=time.time(), boot=boot,
                              elapsed=time.clock_gettime(time.CLOCK_BOOTTIME), awake=started,
                              interval=config["interval_seconds"], screen=screen_state(), brightness=brightness(),
                              process_activity=config["process_activity"])
                current = processes() if config["process_activity"] else {}
                activity = process_delta(previous, current) if previous_sample and contiguous(previous_sample, sample) else []
                store.append(sample, activity)
                previous, previous_sample = current, sample
                if started - last_prune > 3600 or not last_prune:
                    store.prune(time.time() - config["retention_days"] * 86400)
                    last_prune = started
                if once:
                    break
                # No RTC alarm, suspend inhibit, network or hardware writes.
                stopped.wait(max(0, started + config["interval_seconds"] - time.monotonic()))
        finally:
            store.db.close()
