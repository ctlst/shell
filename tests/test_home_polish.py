"""Exercise Home's actual C palette loader without needing a GTK display."""

import os
from pathlib import Path
import re
import shlex
import subprocess

import pytest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ctlsthome/home-widget-runtime.c"


@pytest.fixture(scope="module")
def palette_loader(tmp_path_factory):
    try:
        flags = subprocess.check_output(
            ["pkg-config", "--cflags", "--libs", "glib-2.0"], text=True
        )
    except (FileNotFoundError, subprocess.CalledProcessError):
        pytest.skip("glib-2.0 development files unavailable")
    source = SOURCE.read_text()
    # Compile the production declarations and loader unchanged, excluding only
    # the GTK rendering/process state that this pure palette check does not use.
    declarations = source.split("struct widget_theme_token {", 1)[1].split(
        "struct home_widget_runtime {", 1
    )[0]
    loader = source.split("static bool\nvalid_theme_value", 1)[1].split(
        "static void\nset_source", 1
    )[0]
    harness = (
        '#include "ctlst-runtime.h"\n#include <stdbool.h>\n#include <stdio.h>\n'
        '#define WIDGET_THEME_TOKEN_MAX 16\n'
        'struct widget_theme_token {' + declarations
        + 'struct home_widget_runtime {\n'
        'struct widget_theme_token theme_tokens[G_N_ELEMENTS(k_theme_defaults)];\n};\n'
        + 'static bool\nvalid_theme_value' + loader
        + 'int main(void) {\nstruct home_widget_runtime runtime;\n'
        'load_theme_tokens(&runtime);\n'
        'for (size_t i = 0; i < G_N_ELEMENTS(k_theme_defaults); i++)\n'
        'printf("%s=%s\\n", k_theme_defaults[i].env_name,\n'
        'resolve_color(&runtime, k_theme_defaults[i].name, "invalid"));\n'
        'return 0;\n}\n'
    )
    directory = tmp_path_factory.mktemp("home-palette")
    c_file = directory / "palette.c"
    c_file.write_text(harness)
    binary = directory / "palette"
    subprocess.run(
        ["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
         str(c_file), "-o", str(binary), *shlex.split(flags)], check=True
    )
    return binary


def read_palette(binary, directory, contents):
    (directory / "theme.env").write_text(contents)
    result = subprocess.check_output(
        [str(binary)], text=True,
        env={**os.environ, "CTLST_GENERATED_DIR": str(directory)},
    )
    return dict(line.split("=", 1) for line in result.splitlines())


@pytest.mark.parametrize("reverse", [False, True])
def test_all_widget_tokens_resolve_independently_of_file_order(
    palette_loader, tmp_path, reverse
):
    names = re.findall(r'"@ctlst_\w+", "(SHELL_\w+)"', SOURCE.read_text())
    colors = {name: f"{index + 1:06x}" for index, name in enumerate(names)}
    items = list(colors.items())
    if reverse:
        items.reverse()
    contents = "# generated palette\nUNKNOWN=abcdef\n" + "\n".join(
        f"{name}={value}" for name, value in items
    )
    assert read_palette(palette_loader, tmp_path, contents) == {
        name: f"#{value}ff" for name, value in colors.items()
    }


def test_widget_palette_validation_and_exact_keys(palette_loader, tmp_path):
    palette = read_palette(
        palette_loader, tmp_path,
        "SHELL_TEXT_EXTRA=abcdef\nSHELL_TEXT=12345678\n"
        "SHELL_BG=nothex\nSHELL_ACCENT=abcdef\nSHELL_ACCENT=000000\n",
    )
    assert palette["SHELL_TEXT"] == "#12345678"
    assert palette["SHELL_BG"] == "#171721ff"
    assert palette["SHELL_PANEL"] == "#252535ff"
    assert palette["SHELL_ACCENT"] == "#abcdefff"
