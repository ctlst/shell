"""Exercise real helpers, including idle visibility and release-only media input."""
import importlib.machinery
import importlib.util
import json
import os
from pathlib import Path
import select
import subprocess
import time

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts/ctlst-home-tools'
loader = importlib.machinery.SourceFileLoader('home_tools', str(SCRIPT))
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)

def test_media_compact_controls_match_hit_regions():
    for width, height in [(130,60),(220,95),(220,140),(460,260)]:
        frame = module.scene('media', {'title':'A long track title '*20,'state':'Playing'}, width, height)
        controls = module.media_buttons(width,height)
        assert controls
        for _,x,y,w in controls:
            assert 0 <= x and x+w <= width and 0 <= y and y+44 <= height
        assert any(n.get('text','').startswith('A long') for n in frame['nodes'])

def test_helper_stops_polling_hidden_and_cancels_swipes(tmp_path):
    log=tmp_path/'calls'
    player=tmp_path/'playerctl'
    player.write_text('#!/bin/sh\nprintf "%s\\n" "$*" >> "$CALL_LOG"\ncase "$1" in status) echo Playing;; metadata) echo Track;; esac\n')
    player.chmod(0o755)
    proc=subprocess.Popen(['python3',str(SCRIPT),'media'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True,
                          env={**os.environ,'PATH':str(tmp_path)+os.pathsep+os.environ['PATH'],'CALL_LOG':str(log)})
    def send(**message):
        proc.stdin.write(json.dumps(message)+'\n');proc.stdin.flush()
    try:
        send(type='hello',width=220,height=80)
        deadline=time.monotonic()+5
        while (not log.exists() or len(log.read_text().splitlines())<3) and time.monotonic()<deadline: time.sleep(.02)
        assert log.exists()
        send(type='visibility',visible=False)
        time.sleep(.2); before=log.read_text();time.sleep(2.2)
        assert log.read_text()==before
        send(type='visibility',visible=True)
        time.sleep(.3)
        send(type='pointer',phase='begin',view_x=185,view_y=40,inside=True)
        send(type='pointer',phase='cancel',view_x=185,view_y=40,inside=True)
        send(type='pointer',phase='end',view_x=185,view_y=40,inside=True)
        time.sleep(.2);assert 'play-pause' not in log.read_text()
        send(type='pointer',phase='begin',view_x=185,view_y=40,inside=True)
        send(type='pointer',phase='end',view_x=185,view_y=40,inside=True)
        deadline=time.monotonic()+3
        while 'play-pause' not in log.read_text() and time.monotonic()<deadline:time.sleep(.02)
        assert log.read_text().count('play-pause')==1
    finally:
        send(type='shutdown');proc.wait(timeout=2)
