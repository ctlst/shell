#!/usr/bin/env python3
"""Disposable test window; --refuse deliberately ignores normal close."""
import sys
import gi
gi.require_version("Gtk", "4.0")
from gi.repository import Gtk

app = Gtk.Application(application_id=sys.argv[1])
def activate(application):
    window = Gtk.ApplicationWindow(application=application, title="Close test fixture")
    window.set_child(Gtk.Label(label="Disposable close test — no user data"))
    if "--refuse" in sys.argv:
        window.connect("close-request", lambda *_: True)
    window.present()
app.connect("activate", activate)
app.run([])
