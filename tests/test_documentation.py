"""Public guides must reference files available to a repository checkout."""
from pathlib import Path
import re
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def documents():
    return [path for path in ROOT.rglob("*.md")
            if not any(part.startswith(".") for part in path.relative_to(ROOT).parts)]


def test_local_markdown_links_resolve():
    missing = []
    for path in documents():
        for target in re.findall(r"\[[^\]]*\]\(([^\s)]+)\)", path.read_text()):
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            destination = path.parent / unquote(parsed.path)
            if not destination.exists():
                missing.append(f"{path.relative_to(ROOT)}: {target}")
    assert not missing, "\n".join(missing)


def test_documented_root_test_paths_are_shipped():
    missing = []
    for path in documents():
        for target in re.findall(r"`((?:tests|vm/clean-room)/[\w./-]+\.(?:c|py))`", path.read_text()):
            if not (ROOT / target).is_file():
                missing.append(f"{path.relative_to(ROOT)}: {target}")
    assert not missing, "\n".join(missing)
