"""GTK4 battery history; disk queries run outside the GTK loop."""
import datetime
import os
from pathlib import Path
import threading
import time

os.environ.setdefault("GSK_RENDERER", "gl")
import gi
gi.require_version("Gtk", "4.0")
gi.require_version("Gdk", "4.0")
from gi.repository import Gdk, Gio, GLib, Gtk, Pango
from ctlst_battery import config_path, contiguous, export_csv, report, state_dir


def duration(seconds):
    if seconds is None:
        return "Unavailable"
    minutes = max(0, int(seconds / 60))
    return f"{minutes // 60}h {minutes % 60:02d}m" if minutes >= 60 else f"{minutes}m"


def metric(value, suffix="", digits=1):
    return "Unavailable" if value is None else f"{value:.{digits}f}{suffix}"


def date(timestamp):
    return datetime.datetime.fromtimestamp(timestamp).strftime("%b %d, %H:%M")


def label(text="", css=None):
    widget = Gtk.Label(label=text, xalign=0, wrap=True)
    widget.set_wrap_mode(Pango.WrapMode.WORD_CHAR)
    widget.set_hexpand(True)
    if css:
        widget.add_css_class(css)
    return widget


class Chart(Gtk.DrawingArea):
    """A retained, static plot; redraw only for new data or allocation changes."""
    def __init__(self):
        super().__init__(content_height=180, hexpand=True)
        self.data = None
        self.set_draw_func(self.draw)
        self.update_property([Gtk.AccessibleProperty.LABEL], ["Battery percentage history. Summary below."])

    def draw(self, _area, cr, width, height):
        color = self.get_style_context().get_color()
        left, top, right, bottom = 32, 14, width - 8, height - 24
        cr.set_font_size(10)
        for level in (0, 50, 100):
            y = bottom - (bottom - top) * level / 100
            cr.set_source_rgba(color.red, color.green, color.blue, .45)
            cr.move_to(0, y + 3)
            cr.show_text(str(level))
            cr.set_source_rgba(color.red, color.green, color.blue, .12)
            cr.move_to(left, y)
            cr.line_to(right, y)
            cr.stroke()
        if not self.data:
            return
        start, end = self.data["since"], self.data["now"]
        cr.set_source_rgba(color.red, color.green, color.blue, .65)
        long_range = end - start >= 86400
        for x, stamp in ((left, start), (max(left, right - (79 if long_range else 36)), end)):
            cr.move_to(x, height - 4)
            cr.show_text(datetime.datetime.fromtimestamp(stamp).strftime("%b %d %H:%M" if long_range else "%H:%M"))
        rows = self.data["samples"]
        previous = None
        for row in rows:
            capacity = row["capacity"]
            if capacity is None:
                previous = None
                continue
            x = left + (right - left) * (row["timestamp"] - start) / max(1, end - start)
            y = bottom - (bottom - top) * capacity / 100
            cr.set_source_rgba(color.red, color.green, color.blue, .9)
            if row["status"] in ("Charging", "Full"):
                cr.set_source_rgb(.25, .65, .4)
            if previous and contiguous(previous[0], row):
                cr.set_line_width(2.5)
                cr.move_to(previous[1], previous[2])
                cr.line_to(x, y)
                cr.stroke()
            elif len(rows) < 200:
                cr.arc(x, y, 2.5, 0, 6.2832)
                cr.fill()
            previous = (row, x, y)


class Battery(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="dev.ctlst.Battery")
        self.busy = False
        self.serial = 0
        self.data = None
        self.timer = None
        self.last_signature = None
        self.query_key = None
        self.query_data = None

    def do_startup(self):
        Gtk.Application.do_startup(self)
        provider = Gtk.CssProvider()
        provider.load_from_data(b"""
            @define-color ctlst_bg #171b23;
            @define-color ctlst_text #eef1f7;
            @define-color ctlst_panel #242b36;
            window#ctlstbattery { background: @ctlst_bg; color: @ctlst_text; }
            #ctlstbattery label { color: @ctlst_text; }
            #ctlstbattery button, #ctlstbattery dropdown { min-height: 40px; }
            #ctlstbattery button { background: @ctlst_panel; background-image: none; color: @ctlst_text; border: 1px solid alpha(@ctlst_text,.15); box-shadow: none; }
            #ctlstbattery button:hover { background: alpha(@ctlst_text,.15); }
            #ctlstbattery button:focus-visible { outline: 2px solid @ctlst_text; outline-offset: -3px; }
            #ctlstbattery popover contents, #ctlstbattery popover listview { background: @ctlst_panel; color: @ctlst_text; }
            #ctlstbattery popover row:selected { background: alpha(@ctlst_text,.15); }
            #ctlstbattery .battery-card { padding: 16px; border-radius: 20px; background: alpha(@ctlst_text,.055); }
            #ctlstbattery .battery-heading { font-size: 18px; font-weight: 650; }
            #ctlstbattery .battery-number { font-size: 36px; font-weight: 700; }
            #ctlstbattery .battery-muted { opacity: .72; font-size: 12px; }
        """)
        Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), provider, 600)
        self.theme = Gtk.CssProvider()
        default = Path.home() / ".config/ctlst"
        if not (default / "theme.css").exists():
            default = Path.home() / ".config/sway-touch"
        self.theme_path = Path(os.environ.get("CTLST_GENERATED_DIR", default)) / "theme.css"
        Gtk.StyleContext.add_provider_for_display(Gdk.Display.get_default(), self.theme, 800)
        self.reload_theme()
        try:
            self.monitor = Gio.File.new_for_path(str(self.theme_path.parent)).monitor_directory(Gio.FileMonitorFlags.NONE, None)
            self.monitor.connect("changed", lambda *_: self.reload_theme())
        except GLib.Error:
            self.monitor = None

    def reload_theme(self):
        if self.theme_path.is_file():
            try:
                self.theme.load_from_path(str(self.theme_path))
            except GLib.Error:
                pass

    def do_activate(self):
        if self.props.active_window:
            self.props.active_window.present()
            return
        self.serial += 1
        self.last_signature = None
        window = Gtk.ApplicationWindow(application=self, title="Battery", default_width=480, default_height=820)
        window.set_name("ctlstbattery")
        root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        for edge in ("top", "bottom", "start", "end"):
            getattr(root, "set_margin_" + edge)(16)
        header = Gtk.Box(spacing=8)
        header.append(label("Battery", "battery-heading"))
        close = Gtk.Button(label="Close")
        close.connect("clicked", lambda *_: window.close())
        header.append(close)
        root.append(header)
        self.ranges = Gtk.DropDown.new_from_strings(["Last 6 hours", "Last 24 hours", "Last 7 days", "Last 30 days", "Since last full"])
        self.ranges.set_selected(1)
        self.ranges.update_property([Gtk.AccessibleProperty.LABEL], ["History range"])
        self.ranges.connect("notify::selected", self.change_range)
        root.append(self.ranges)
        self.status = label("Reading history…", "battery-muted")
        root.append(self.status)
        scroll = Gtk.ScrolledWindow(vexpand=True)
        scroll.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        scroll.set_kinetic_scrolling(True)
        content = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        scroll.set_child(content)
        root.append(scroll)
        overview = self.card(content, "Charge history")
        self.level = label("—", "battery-number")
        self.state = label()
        self.full = label()
        self.chart = Chart()
        self.history_summary = label("Waiting for samples", "battery-muted")
        for item in (self.level, self.state, self.full, self.chart, self.history_summary):
            overview.append(item)
        overview.append(label("Green: charging/full · gaps: no continuous observations", "battery-muted"))
        drain = self.card(content, "Discharge & screen")
        self.drain = label()
        drain.append(self.drain)
        drain.append(label("Power and energy are estimates from battery telemetry. Screen time is sampled; transitions and missing observations are excluded.", "battery-muted"))
        activity = self.card(content, "Activity while discharging")
        activity.append(label("CPU time, not battery %. Display, GPU, modem and radio power cannot be assigned to apps here. Short-lived or unreadable processes may be missing.", "battery-muted"))
        self.activity = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        activity.append(self.activity)
        health = self.card(content, "Battery details")
        self.health = label()
        health.append(self.health)
        events = self.card(content, "Charging timeline")
        self.events = label()
        events.append(self.events)
        controls = self.card(content, "History storage")
        controls.append(label("Recorded locally every 60 seconds by default; retained for 30 days. Recording continues with the screen off while your session is running. It never wakes a suspended phone.", "battery-muted"))
        controls.append(label(f"Configuration: {config_path()}\nData: {state_dir()}\nRestart the recorder after changing configuration.", "battery-muted"))
        export = Gtk.Button(label="Export selected history (CSV)")
        export.connect("clicked", self.export)
        controls.append(export)
        controls.append(label("Exports include battery samples, not process names. CLI JSON reports also include process activity; review before sharing.", "battery-muted"))
        window.set_child(root)
        key = Gtk.EventControllerKey()
        key.connect("key-pressed", self.key)
        window.add_controller(key)
        window.connect("close-request", self.closed)
        window.present()
        self.refresh()
        self.timer = GLib.timeout_add_seconds(15, self.tick)

    def card(self, parent, title):
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
        box.add_css_class("battery-card")
        box.append(label(title, "battery-heading"))
        parent.append(box)
        return box

    def key(self, _controller, keyval, _keycode, _modifiers):
        if keyval == Gdk.KEY_Escape:
            self.props.active_window.close()
            return True
        return False

    def closed(self, *_):
        self.serial += 1
        if self.timer:
            GLib.source_remove(self.timer)
            self.timer = None
        return False

    def change_range(self, *_):
        self.serial += 1
        self.last_signature = None
        self.refresh()

    def tick(self):
        window = self.props.active_window
        if window and window.is_active():
            self.refresh()
        return GLib.SOURCE_CONTINUE

    def refresh(self):
        if self.busy:
            return
        serial = self.serial
        hours = (6, 24, 168, 720, 0)[self.ranges.get_selected()]
        self.busy = True
        def worker():
            try:
                path = state_dir() / "history.sqlite3"
                key = (hours, path.stat().st_mtime_ns if path.exists() else None)
                data = self.query_data if key == self.query_key and self.query_data is not None else report(hours)
                self.query_data = data
                self.query_key = key
                error = None
            except Exception as exc:
                data, error = None, str(exc)
            GLib.idle_add(done, data, error)
        def done(data, error):
            self.busy = False
            if not self.props.active_window:
                return False
            if serial != self.serial:
                self.refresh()
                return False
            if error:
                self.status.set_text("History unavailable: " + error)
                return False
            self.render(data)
            return False
        threading.Thread(target=worker, daemon=True, name="battery-history").start()

    def render(self, data):
        self.data = data
        current = data["latest"]
        if not current:
            self.status.set_text("No history yet. Start the recorder with: ctlst-battery daemon")
            self.state.set_text("History starts when recording begins; earlier charges cannot be reconstructed.")
            return
        age = time.time() - current["timestamp"]
        stale = age > current["interval"] * 2.5
        self.status.set_text(("Recorder has not updated · " if stale else "History updated · ") + "Last sample " + date(current["timestamp"]))
        self.level.set_text(metric(current.get("capacity"), "%", 0))
        self.state.set_text(f"{current['status']} · {metric(current.get('watts'), ' W')} · {metric(current.get('temp'), ' °C')}")
        self.full.set_text("Last observed full: " + (date(data["last_full"]) + " · " + duration(time.time() - data["last_full"]) + " ago" if data["last_full"] else "Not observed yet"))
        self.chart.data = data
        signature = (current["timestamp"], self.ranges.get_selected())
        if signature != self.last_signature:
            self.chart.queue_draw()
        summary = data["summary"]
        self.history_summary.set_text(f"{len(data['samples'])} samples · {duration(summary['covered_seconds'])} continuous coverage in selected range")
        def average(prefix):
            seconds = summary[prefix + "_power_seconds"]
            return metric(summary[prefix + "_wh"] * 3600 / seconds if seconds else None, " W")
        energy = metric(summary["energy_wh"] if summary["energy_seconds"] else None, " Wh", 2)
        eta = "Learning discharge rate (needs ≥15 min and ≥1% drop)" if summary["remaining_hours"] is None else duration(summary["remaining_hours"] * 3600) + " remaining (recent trend)"
        if stale:
            eta = "Recorder has not updated; remaining-time estimate unavailable"
        self.drain.set_text(f"{eta}\nAverage drain: {metric(summary['rate_per_hour'], '%/hour')}\nEstimated discharge energy: {energy}\nScreen on: {duration(summary['screen_on_seconds'])} / {duration(summary['screen_known_seconds'])} observed\nDisplay-on total power: {average('screen_on')}\nDisplay-off total power: {average('screen_off')}")
        if signature != self.last_signature:
            while self.activity.get_first_child():
                self.activity.remove(self.activity.get_first_child())
            if not data["activity"]:
                self.activity.append(label("Process recording disabled." if not current.get("process_activity", True) else "Waiting for consecutive discharging samples.", "battery-muted"))
            for name, seconds in data["activity"]:
                row = label(f"{name}  ·  {seconds:.1f} CPU seconds")
                self.activity.append(row)
        self.last_signature = signature
        self.health.set_text(f"Device: {current.get('device') or 'No system battery detected'}\nVoltage: {metric(current.get('voltage_now'), ' V', 3)}\nCurrent (driver sign): {metric(current.get('current_now'), ' A', 3)}\nBrightness: {metric(current.get('brightness'), '%', 0)}\nDesign capacity: {metric(current.get('charge_full_design'), ' mAh', 0)}\nMeasured full capacity: {metric(current.get('charge_full'), ' mAh', 0)}\nFull/design capacity: {metric(current.get('wear_percent'), '%')}\nCycles: {metric(current.get('cycle_count'), '', 0)}\nHealth: {current.get('health') or 'Not exposed by driver'}")
        self.events.set_text("\n".join(f"{date(e['timestamp'])} · {e['kind']} · {metric(e['capacity'], '%', 0)}" for e in data["events"][:20]) or "No charging transitions in this range.")

    def export(self, *_):
        if not self.data or not self.data["samples"]:
            self.status.set_text("No samples to export in this range.")
            return
        # Snapshot the chosen range; later refreshes must not change the export.
        text = export_csv(self.data)
        chooser = Gtk.FileChooserNative.new("Export battery history", self.props.active_window,
                                            Gtk.FileChooserAction.SAVE, "Export", "Cancel")
        chooser.set_current_name("battery-history.csv")
        def response(dialog, response_id):
            if response_id == Gtk.ResponseType.ACCEPT:
                target = dialog.get_file()
                if target:
                    def completed(file, result):
                        try:
                            file.replace_contents_finish(result)
                            message = "Battery history exported."
                        except GLib.Error as error:
                            message = "Export failed: " + str(error)
                        if self.props.active_window:
                            self.status.set_text(message)
                    target.replace_contents_async(text.encode(), None, False, Gio.FileCreateFlags.PRIVATE, None, completed)
            dialog.destroy()
        chooser.connect("response", response)
        chooser.show()


def run():
    return Battery().run([])
