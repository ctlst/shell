from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TOP = (ROOT / "Makefile").read_text(encoding="utf-8")


def test_core_build_has_an_out_of_tree_output_for_each_owned_surface():
    for component in (
        "ctlst-gestured",
        "ctlstaction",
        "ctlstdock",
        "ctlsthome",
        "ctlstlock",
        "ctlstoverview",
        "ctlstshade",
    ):
        assert component in TOP
        makefile = (ROOT / component / "Makefile").read_text(encoding="utf-8")
        assert f"OUTPUT ?= {component}" in makefile
        assert "mkdir -p $(@D)" in makefile


def test_core_does_not_build_optional_phone_or_game_packages():
    components = TOP.split("CORE_COMPONENTS :=", 1)[1].splitlines()[0]

    assert "ctlstdialer" not in components
    assert "ctlstmessages" not in components
    assert "ctlstpad" not in components


def test_build_outputs_are_kept_out_of_the_source_directories():
    assert "CORE_BIN_DIR := $(abspath $(BUILD_DIR)/bin)" in TOP
    assert "$(MAKE) -C ctlstdock OUTPUT=$@" in TOP


def test_core_install_uses_a_staged_distribution_layout():
    assert "install: core" in TOP
    assert '"$(DESTDIR)$(BINDIR)"' in TOP
    assert '"$(DESTDIR)$(LIBEXECDIR)"' in TOP
    assert '"$(DESTDIR)$(DATADIR)/defaults"' in TOP
    assert '"$(DESTDIR)$(SESSIONDIR)"' in TOP
    assert "session/ctlst-session" in TOP
    assert "session/ctlst.desktop" in TOP
    assert "sway/ctlst.conf" in TOP
    assert "LICENSE NOTICE THIRD_PARTY.md" in TOP


def test_core_install_does_not_include_optional_application_packages():
    install = TOP.split("install: core", 1)[1].split("\ncheck:", 1)[0]

    assert "ctlstdialer" not in install
    assert "ctlstmessages" not in install
    assert "ctlst-smsd" not in install
    assert "wvkbd-swaytouch" not in install
    assert "ctlstfiles" not in install
    assert "ctlstpad" not in install
