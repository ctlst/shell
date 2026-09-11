"""Guard the generic starting point; device profiles may opt into more."""

import configparser
from pathlib import Path


DEFAULTS = Path(__file__).resolve().parents[1] / "config" / "defaults"


def read_defaults(name):
    config = configparser.ConfigParser()
    assert config.read(DEFAULTS / name)
    return config


def test_generic_phone_apps_are_user_choices():
    apps = read_defaults("applications.conf")["applications"]
    assert apps["phone"] == "none"
    assert apps["messages"] == "none"
    assert apps["files"] == "xdg"


def test_generic_defaults_do_not_impose_device_memory_policy():
    memory = read_defaults("memory.conf")
    assert not memory.getboolean("lifecycle", "enabled")
    assert not memory.getboolean("lifecycle", "manage_flatpak")
    assert not memory.getboolean("pressure", "enabled")
    for section in memory.sections():
        for key, value in memory[section].items():
            if key.endswith("_bytes"):
                assert int(value) == 0, (section, key)


def test_generic_theme_is_a_plain_packaged_choice():
    theme = read_defaults("theme.conf")["theme"]
    root = DEFAULTS.parents[1]
    assert (root / "themes" / theme["name"] / "theme.env").is_file()
    assert theme["accent"] == "theme", "generic defaults must follow the chosen palette"
