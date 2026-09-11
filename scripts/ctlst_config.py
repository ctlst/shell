"""Shared CTLST configuration and installed paths; no shell evaluation."""
import argparse
import configparser
import json
import math
import os
import re
from pathlib import Path

INI_SCOPES = ("session", "input", "gestures", "applications", "memory", "theme", "notes")
TEXT_SCOPES = {"sway": ".conf", "home": ".yaml", "bar": ".jsonc", "bar-style": ".css",
               "theme-style": ".css"}


def xdg_config_home() -> Path:
    configured = os.environ.get("XDG_CONFIG_HOME")
    return Path(configured) if configured else Path.home() / ".config"


def data_root() -> Path:
    configured = os.environ.get("CTLST_DATA_DIR")
    if configured:
        return Path(configured)
    installed = Path("/usr/share/ctlst-shell")
    if installed.is_dir():
        return installed
    return Path(__file__).resolve().parents[1]


def data_defaults() -> Path:
    root = data_root()
    installed_layout = root / "defaults"
    if installed_layout.is_dir():
        return installed_layout
    return root / "config" / "defaults"


def profile_root() -> Path:
    return Path(os.environ.get("CTLST_PROFILE_DIR", "/usr/lib/ctlst-shell/profile.d"))


def system_config_root() -> Path:
    return Path(os.environ.get("CTLST_SYSTEM_CONFIG_DIR", "/etc/ctlst"))


def suffix(scope: str) -> str:
    return TEXT_SCOPES.get(scope, ".conf")


def user_path(scope: str) -> Path:
    return xdg_config_home() / "ctlst" / f"{scope}{suffix(scope)}"


def config_paths(scope: str) -> list[tuple[str, Path]]:
    paths: list[tuple[str, Path]] = [
        ("packaged default", data_defaults() / f"{scope}{suffix(scope)}")
    ]
    root = profile_root()
    if root.is_dir():
        for profile in sorted(root.iterdir()):
            candidate = profile / f"{scope}{suffix(scope)}"
            if profile.is_dir() and (candidate.exists() or candidate.is_symlink()):
                paths.append((f"profile {profile.name}", candidate))
    paths.extend(
        [
            ("system", system_config_root() / f"{scope}{suffix(scope)}"),
            ("user", user_path(scope)),
        ]
    )
    return paths


def known_scope(value: str) -> str:
    if value not in (*INI_SCOPES, *TEXT_SCOPES):
        raise argparse.ArgumentTypeError(f"unknown configuration area: {value}")
    return value


def read_layers(scope: str) -> tuple[configparser.ConfigParser, dict[tuple[str, str], str]]:
    parser = configparser.ConfigParser(interpolation=None)
    origins: dict[tuple[str, str], str] = {}
    for label, path in config_paths(scope):
        if not path.is_file():
            if path.exists() or path.is_symlink():
                raise ValueError(f"{scope}: expected a readable configuration file: {path}")
            continue
        layer = configparser.ConfigParser(interpolation=None)
        with path.open(encoding="utf-8") as stream:
            layer.read_file(stream, source=str(path))
        parser.read(path, encoding="utf-8")
        for section in layer.sections():
            for key in layer[section]:
                origins[(section, key)] = label
    return parser, origins


def libexec_root() -> Path:
    return Path(os.environ.get("CTLST_LIBEXEC_DIR", Path(__file__).resolve().parent))


def runtime_root() -> Path:
    return Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))


def generated_root() -> Path:
    return runtime_root() / "ctlst-shell" / "generated"


def validate(scope, parser):
    if scope == "notes":
        if not parser.get("notes", "file").strip():
            raise ValueError("notes: file must not be empty")
        editor = json.loads(parser.get("notes", "editor"))
        if not isinstance(editor, list) or any(not isinstance(x, str) or not x or "\0" in x for x in editor):
            raise ValueError("notes: editor must be a JSON command-argument array")
    defaults = configparser.ConfigParser(interpolation=None)
    defaults.read(data_defaults() / f"{scope}.conf")
    for section in parser.sections():
        if section not in defaults:
            raise ValueError(f"{scope}: unknown section {section}")
        for key, value in parser[section].items():
            if key not in defaults[section]:
                raise ValueError(f"{scope}: unknown key {section}.{key}")
            prototype = defaults[section][key]
            if prototype in {"true", "false"}:
                parser.getboolean(section, key)
            elif re.fullmatch(r"[0-9]+", prototype):
                if not value.isdigit():
                    raise ValueError(f"{scope}: {section}.{key} requires a nonnegative integer")
            elif re.fullmatch(r"[0-9]+\.[0-9]+", prototype):
                number = float(value)
                if not math.isfinite(number) or number <= 0:
                    raise ValueError(f"{scope}: {section}.{key} requires a positive finite number")
            elif any(c in value for c in ("\n", "\r", "\x00")):
                raise ValueError(f"{scope}: multiline values are unsupported")
    if scope == "memory":
        for section in ("app", "terminal"):
            high = parser.getint(section, "memory_high_bytes")
            maximum = parser.getint(section, "memory_max_bytes")
            if maximum and high > maximum:
                raise ValueError(f"memory: {section} high exceeds maximum")
        # These policies are not implemented by the portable launcher yet.
        for section, key in (("lifecycle", "enabled"), ("lifecycle", "manage_flatpak"),
                             ("pressure", "enabled")):
            if parser.getboolean(section, key):
                raise ValueError(f"memory: {section}.{key} is not supported yet; use explicit per-app limits")
        if parser.getint("compositor", "restart_rss_bytes") or parser.getint("compositor", "restart_swap_bytes"):
            raise ValueError("compositor memory restarts are not supported")
    if scope == "session" and parser.get("session", "compositor") != "sway":
        raise ValueError("only upstream Sway is supported")
    if scope == "session" and not 1 <= parser.getint("session", "launch_timeout_seconds") <= 300:
        raise ValueError("session: launch_timeout_seconds must be between 1 and 300")
    if scope == "input":
        layout = parser.get("input", "keyboard_layout")
        if not re.fullmatch(r"[A-Za-z0-9_,+-]+", layout):
            raise ValueError("input: invalid keyboard_layout")
        for key in ("keyboard_repeat_rate_hz", "keyboard_repeat_delay_ms"):
            if parser.getint("input", key) > 10000:
                raise ValueError(f"input: {key} exceeds supported range")
    if scope == "theme":
        if not re.fullmatch(r"[A-Za-z0-9_-]+", parser.get("theme", "name")):
            raise ValueError("theme: invalid theme name")
        accent = parser.get("theme", "accent")
        if accent != "theme" and not re.fullmatch(r"[0-9a-fA-F]{6}", accent):
            raise ValueError("theme: accent must be 'theme' or six hexadecimal digits")


def resolved(scope):
    parser, origins = read_layers(scope)
    validate(scope, parser)
    return parser, origins


def component_environment():
    environment = os.environ.copy()
    environment["CTLST_LIBEXEC_DIR"] = str(libexec_root())
    environment["CTLST_DATA_DIR"] = str(data_root())
    environment["CTLST_CONFIG_DIR"] = str(xdg_config_home() / "ctlst")
    environment["CTLST_GENERATED_DIR"] = str(generated_root())
    environment["CTLST_HOME_CONFIG_PATHS"] = os.pathsep.join(str(p) for _, p in config_paths("home") if p.is_file())
    session, _ = resolved("session")
    environment["CTLST_LAUNCH_TIMEOUT_SECONDS"] = session.get("session", "launch_timeout_seconds")
    inputs, _ = resolved("input")
    for key, variable in (("output", "CTLST_OUTPUT_NAME"), ("touch_device", "CTLST_TOUCH_DEVICE"),
                          ("haptic_device", "CTLST_HAPTIC_DEVICE")):
        value = inputs.get("input", key)
        if value != "auto":
            environment[variable] = value
    gestures, _ = resolved("gestures")
    for section, key, variable in (
        ("drawer", "pull_bottom_inset_px", "CTLST_DRAWER_PULL_BOTTOM_INSET"),
        ("drawer", "tracking_gain", "CTLST_DRAWER_PULL_TRACKING_GAIN"),
        ("drawer", "commit_distance_px", "CTLST_DRAWER_PULL_COMMIT_DISTANCE"),
        ("shade", "tracking_gain", "CTLST_SHADE_PULL_TRACKING_GAIN"),
        ("shade", "commit_distance_px", "CTLST_SHADE_PULL_COMMIT_DISTANCE"),
        ("shade", "home_pull_start_fraction", "CTLST_SHADE_HOME_PULL_START_DEPTH"),
        ("dock", "scroll_detent", "CTLST_DOCK_SCROLL_DETENT"),
        ("dock", "scroll_rate_limit_ms", "CTLST_DOCK_SCROLL_RATE_LIMIT_MS"),
    ):
        environment[variable] = gestures.get(section, key)
    return environment
