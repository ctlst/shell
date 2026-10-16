"""Generated modifier ink remains readable without rewriting authored palettes."""
from pathlib import Path
import runpy
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
RENDERER = ROOT / "scripts/ctlst-theme-render"


def test_modifier_contrast_and_palette_preservation(tmp_path):
    renderer = runpy.run_path(str(RENDERER))
    for name, background, foreground, fill, ink in (
        ("dark", "0b0c16ff", "d7d9e4ff", "151828ff", "0b0c16ff"),
        ("light", "ffffffff", "202020ff", "eeeeeeff", "ffffffff"),
        ("readable", "0b0c16ff", "d7d9e4ff", "151828ff", "d7d9e4ff"),
    ):
        source = tmp_path / name
        source.mkdir()
        text = f"THEME_MODE=dark\nKEYBOARD_BG={background}\nKEYBOARD_TEXT={foreground}\nKEYBOARD_FG_SPECIAL={fill}\nKEYBOARD_TEXT_SPECIAL={ink}\n"
        (source / "theme.env").write_text(text)
        output = tmp_path / (name + "-output")
        result = subprocess.run([sys.executable, str(RENDERER), str(source),
                                 str(ROOT / "themes"), str(output)], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        values = renderer["parse_env"](output / "theme.env")
        assert renderer["contrast_ratio"](values["KEYBOARD_TEXT_SPECIAL"], fill) >= 4.5
        if name == "readable":
            assert values["KEYBOARD_TEXT_SPECIAL"] == ink
        assert (source / "theme.env").read_text() == text
