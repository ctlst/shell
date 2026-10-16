"""Synthetic telemetry only; no battery, session or user-state writes."""
import importlib.util
import json
from pathlib import Path
import sqlite3
import stat

import pytest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("battery_backend_test", ROOT / "scripts/ctlst_battery.py")
b = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(b)


def sample(t, capacity=70, **kwargs):
    value = dict(timestamp=10000 + t, elapsed=t, awake=t, boot="boot-a", interval=60,
                 device="test", status="Discharging", capacity=capacity, watts=2,
                 temp=27, screen=1, brightness=50, process_activity=True)
    value.update(kwargs)
    return value


def test_telemetry_units_and_missing_health(tmp_path):
    device = tmp_path / "test"
    device.mkdir()
    (device / "type").write_text("Battery")
    (device / "uevent").write_text("POWER_SUPPLY_STATUS=Discharging\nPOWER_SUPPLY_CAPACITY=42\nPOWER_SUPPLY_VOLTAGE_NOW=4000000\nPOWER_SUPPLY_CURRENT_NOW=-500000\nPOWER_SUPPLY_TEMP=270\nPOWER_SUPPLY_CHARGE_FULL_DESIGN=3700000\n")
    row = b.telemetry(root=tmp_path)
    assert row["watts"] == 2 and row["temp"] == 27
    assert row["charge_full_design"] == 3700
    assert row["wear_percent"] is None and row["cycle_count"] is None
    assert b.telemetry("missing", tmp_path)["status"] == "Unavailable"


def test_no_battery_or_peripheral(tmp_path):
    device = tmp_path / "mouse"
    device.mkdir()
    (device / "type").write_text("Battery")
    (device / "scope").write_text("Device")
    assert b.telemetry(root=tmp_path)["capacity"] is None


@pytest.mark.parametrize("value", ["NaN", "inf", "", "oops", None])
def test_invalid_numbers(value):
    assert b.number(value) is None


def test_config_defaults_and_user_options(tmp_path):
    path = tmp_path / "battery.conf"
    assert b.settings(path)["interval_seconds"] == 60
    path.write_text("[battery]\ninterval_seconds=120\nretention_days=7\nprocess_activity=false\ndevice=BAT1\n")
    assert b.settings(path) == dict(interval_seconds=120, retention_days=7, process_activity=False, device="BAT1")


@pytest.mark.parametrize("setting", ["interval_seconds=0", "interval_seconds=601", "retention_days=91", "device=../foo", "process_activity=maybe"])
def test_bad_config_rejected(tmp_path, setting):
    path = tmp_path / "battery.conf"
    path.write_text("[battery]\n" + setting)
    with pytest.raises(ValueError):
        b.settings(path)


def test_pid_reuse_exits_and_aggregation():
    previous = {(1, 1): ("app", 5), (2, 1): ("app", 3), (3, 1): ("gone", 2)}
    current = {(1, 2): ("reused", 10000), (2, 1): ("app", 7), (4, 1): ("new", 500)}
    assert b.process_delta(previous, current) == [("app", 4)]


def test_proc_stat_parentheses_and_ticks(tmp_path):
    device = tmp_path / "21"
    device.mkdir()
    fields = ["0"] * 22
    fields[0], fields[11], fields[12], fields[19] = "S", "100", "200", "1234"
    (device / "stat").write_text("21 (a strange ) name) " + " ".join(fields))
    assert b.processes(tmp_path)[(21, 1234)][0] == "a strange ) name"
    assert b.processes(tmp_path)[(21, 1234)][1] == 300 / b.os.sysconf("SC_CLK_TCK")


def test_store_persistence_full_events_retention_and_privacy(tmp_path):
    store = b.Store(tmp_path / "state")
    store.append(sample(0, 100, status="Full"))
    store.append(sample(60, 99), [("editor", 2)])
    store.append(sample(120, 98), [("editor", 3)])
    store.db.close()
    result = b.report(directory=tmp_path / "state", now=10120)
    assert result["last_full"] == 10000
    assert result["activity"] == [("editor", 3)]  # first discharge interval is mixed
    assert len(result["events"]) == 2
    assert stat.S_IMODE((tmp_path / "state").stat().st_mode) == 0o700
    assert stat.S_IMODE((tmp_path / "state/history.sqlite3").stat().st_mode) == 0o600
    store = b.Store(tmp_path / "state")
    store.prune(10100)
    assert store.db.execute("SELECT count(*) FROM samples").fetchone()[0] == 1
    assert store.db.execute("SELECT count(*) FROM activity").fetchone()[0] == 1
    store.db.close()
    assert b.report(directory=tmp_path / "state", now=10120)["last_full"] == 10000


@pytest.mark.parametrize("change", [dict(boot="boot-b"), dict(device="other"), dict(elapsed=600), dict(timestamp=12000)])
def test_gaps_are_not_integrated(change):
    rows = [sample(0), sample(60, **change)]
    assert b.summarize(rows, 10060)["covered_seconds"] == 0


def test_rate_energy_screen_time_and_eta():
    rows = [sample(t, 80 - t / 600) for t in range(0, 1860, 60)]
    result = b.summarize(rows, 11800)
    assert result["rate_per_hour"] == pytest.approx(6)
    assert result["energy_wh"] == pytest.approx(1)
    assert result["remaining_hours"] == pytest.approx(77 / 6)
    assert result["screen_on_seconds"] == 1800
    assert b.summarize(rows, 13000)["remaining_hours"] is None


def test_short_flat_or_charging_history_has_no_eta():
    for rows in ([sample(0), sample(60, 69)], [sample(0), sample(60)],
                 [sample(t, status="Charging") for t in range(0, 1860, 60)]):
        assert b.summarize(rows, rows[-1]["timestamp"])["remaining_hours"] is None


def test_mixed_or_unknown_screen_does_not_claim_screen_time():
    result = b.summarize([sample(0), sample(60, screen=0), sample(120, screen=None)], 10120)
    assert result["screen_known_seconds"] == 0
    assert result["energy_wh"] > 0


def test_unknown_initial_full_and_exports(tmp_path):
    store = b.Store(tmp_path)
    store.append(sample(0))
    store.db.close()
    result = b.report(0, tmp_path, 10000)
    assert result["last_full"] is None and result["since"] == 10000 - 86400
    assert "timestamp,device,status,capacity,watts,temp,screen,brightness" in b.export_csv(result)
    assert "process_activity" not in b.export_csv(result)
    json.dumps(result, allow_nan=False)


def test_future_schema_not_overwritten(tmp_path):
    store = b.Store(tmp_path)
    store.db.execute("PRAGMA user_version=99")
    store.db.close()
    with pytest.raises(RuntimeError, match="preserved"):
        b.Store(tmp_path)
    with sqlite3.connect(tmp_path / "history.sqlite3") as db:
        assert db.execute("PRAGMA user_version").fetchone()[0] == 99


def test_empty_report_does_not_create_files(tmp_path):
    path = tmp_path / "missing"
    assert b.report(directory=path)["latest"] is None
    assert not path.exists()
