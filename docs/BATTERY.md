# Battery history

`ctlst-battery` provides a GTK4 dashboard and an independent Python/SQLite
recorder. Open **Battery** in the app drawer or **Settings → Battery → Open
battery history**. Closing the window does not stop the recorder.

## Collection and coverage

The session starts `ctlst-battery daemon`. The recorder reads standard Linux
power-supply attributes and samples every 60 seconds. It retains 30 days by
default. It runs as the user, makes no hardware changes, does not use the
network, request root, inhibit suspend or schedule wake alarms. A file lock
prevents duplicate recorders, including an optional systemd user service.

On systemd installations, `systemctl --user enable --now ctlst-battery.service`
starts recording with the user manager and restarts a failed recorder. This
does not enable lingering or guarantee recording before login. Session-only
autostart ends with the session; screen-off collection continues while the
session runs. Suspended, logged-out, crashed and reboot intervals are not
silently filled in. Persistent history survives recorder and system restarts.

The dashboard provides 6-hour, 24-hour, 7-day, 30-day and since-full ranges,
percentage history, charge-state transitions, last observed full timestamp,
sampled screen-on time, discharge rate, estimated energy, display-on/off total
power comparisons, process CPU activity and available battery diagnostics.
Charging intervals in the timeline begin/end at observations, not exact cable
events. “Not charging” is distinct from “Discharging.”

Time since full begins only after observing `Full` or 100%; it is refreshed
while held full. Previous charges cannot be reconstructed. Since-full falls
back to 24 hours until a full sample exists and remains limited by retention.
Only the selected battery contributes; automatic selection chooses the first
system battery alphabetically and excludes peripheral batteries with Device
scope. Multi-battery systems should select the intended supply explicitly.

## Measurement limits

- Voltage, current, temperature and capacity are driver readings. Watts are
  driver power or voltage × current. Charge/discharge direction uses reported
  status, not an assumed current-sign convention.
- Energy integrates adjacent power samples with the trapezoidal rule. It is
  an estimate, not a hardware energy counter. It excludes missing intervals,
  battery changes, boots, clock jumps and intervals longer than 2.5 sample periods.
- The remaining-time estimate uses up to two hours of uninterrupted discharge
  with at least 15 minutes and one percentage point of loss. Load changes can
  invalidate that estimate. The selected-range average includes net percentage
  changes, including gauge corrections, rather than summing only drops.
- Screen state comes from bounded Sway GET_OUTPUTS requests, never repeated
  GET_TREE queries. Screen-time and display-on/off power comparisons include
  only intervals whose two endpoint states agree. Power is whole-device power
  under those conditions, not the display's individual consumption.
- Process activity is user+kernel CPU time from procfs, keyed by PID and start
  time to avoid PID-reuse spikes. The largest 32 process-name totals per sample
  are retained; the dashboard ranks 20 across discharging intervals. Names can
  be shared by multiple apps. Processes that exit between samples, unreadable
  processes and non-CPU consumers are not attributed. **CPU time is not battery
  percentage or per-app watts.** GPU, display and modem accounting need separate
  device providers; this component does not invent it.
- Health, cycles and measured full/design capacity are shown only when exposed.
  Design capacity alone does not measure degradation.

The reference Pixel 3a XL currently reports capacity, voltage, current,
temperature and nominal design charge. It does not expose measured full charge,
cycle count or per-app energy through this interface.

## Configuration

Copy the packaged `battery.conf` example to `~/.config/ctlst/battery.conf`.
The recorder reads this file on startup; no shell evaluation occurs.

```ini
[battery]
interval_seconds = 60
retention_days = 30
process_activity = true
device = auto
```

`interval_seconds` accepts 30–600; `retention_days` accepts 1–90.
`process_activity=false` stops future process-name collection without erasing
existing records. `device` accepts `auto` or an exact power-supply directory
name. Missing keys use the defaults above; invalid values fail with an error.
XDG_CONFIG_HOME and XDG_STATE_HOME are respected. With a user service, restart
it after edits. Otherwise restart the session recorder.

## CLI and storage

```sh
ctlst-battery status                    # Current hardware readings; no recording
ctlst-battery config-check              # Validate and print effective settings
ctlst-battery report --hours 24         # JSON, including process activity
ctlst-battery export --hours 168 --format csv  # Samples to stdout
ctlst-battery sample                    # One sample, unless recorder already owns lock
ctlst-battery daemon                    # Foreground recorder; Ctrl+C stops it
```

Data lives in `~/.local/state/ctlst/battery/history.sqlite3` (schema version 1),
inside a private 0700 directory with a 0600 database. Only this component's old
records are pruned. SQLite reuses freed space; pruning need not shrink the file.
The full timestamp persists beyond history retention. There is no upload or
automatic external telemetry. Process names can reveal app use; no command
lines, window titles, messages, browsing content or network addresses are read.
Treat the database and JSON exports as private. CSV exports contain only sample
telemetry; the GUI uses a save chooser and private file creation.

GTK uses the shell theme and the GL renderer by default, with diagnostic
GSK_RENDERER overrides preserved. History queries run on a worker; native
kinetic scrolling remains responsive. The static history chart rasterizes only
on data/allocation changes. Back/Close, range selection and export are reachable
by touch, mouse or Tab/Enter; Escape closes the window. Background windows stop
querying history, independently of the recorder.

## Sources and tests

Units follow the [Linux power-supply ABI](https://docs.kernel.org/power/power_supply_class.html).
Process counters follow [proc_pid_stat](https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html).
`tests/test_battery.py` covers normalization, unknown fields, configuration,
process identity, history persistence, events, privacy, retention, interval
coverage, rate estimates and exports with isolated fixtures.
