"""Navigation dispatch must not page behind applications or modal surfaces."""
import os
from pathlib import Path
import runpy
import socket
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "scripts/ctlst-shortcut"


class ShortcutTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.runtime = Path(self.temp.name)
        self.env = patch.dict(os.environ, XDG_RUNTIME_DIR=str(self.runtime))
        self.env.start()
        self.addCleanup(self.env.stop)
        self.main = runpy.run_path(str(SCRIPT))["main"]
        self.server = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.server.bind(str(self.runtime / "ctlst-home.sock"))
        self.server.settimeout(.02)
        self.addCleanup(self.server.close)

    def mark(self, name):
        (self.runtime / name).touch()

    def test_home_pages_send_distinct_commands(self):
        self.mark("ctlsthome.visible")
        for action, message in (("page-prev", b"["), ("page-next", b"]")):
            self.main(action)
            self.assertEqual(self.server.recv(16), message)

    def test_no_paging_behind_app_or_overlay(self):
        self.main("page-next")
        with self.assertRaises(socket.timeout):
            self.server.recv(16)
        self.mark("ctlsthome.visible")
        for name in ("ctlstlock.visible", "ctlstshade.visible", "ctlstdrawer.visible", "ctlstoverview.visible"):
            self.mark(name)
            self.main("page-next")
            with self.assertRaises(socket.timeout):
                self.server.recv(16)
            (self.runtime / name).unlink()

    def test_actions_use_installed_siblings(self):
        with patch("subprocess.run") as run:
            for action, args in (("home", ["ctlst-workspaced", "home"]),
                                 ("shade", ["ctlstshade", "show"]),
                                 ("drawer", ["ctlstdrawer"])):
                self.main(action)
                run.assert_called_with([str(SCRIPT.parent / args[0]), *args[1:]], check=True)

    def test_lock_blocks_all_actions(self):
        self.mark("ctlstlock.visible")
        with patch("subprocess.run") as run:
            for action in ("home", "shade", "drawer", "page-prev", "page-next"):
                self.main(action)
            run.assert_not_called()

    def test_unknown_action_rejected(self):
        with self.assertRaises(ValueError):
            self.main("launch-anything")


if __name__ == "__main__":
    unittest.main()
