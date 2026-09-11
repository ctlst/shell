"""Production privacy surface: real touch, revealed background and dismissal."""
import argparse
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time
import gi
gi.require_version('GdkPixbuf', '2.0')
from gi.repository import GdkPixbuf

p = argparse.ArgumentParser()
p.add_argument('--disposable-vm', action='store_true', required=True)
p.add_argument('--binary', required=True)
p.add_argument('--touch-inject', required=True)
a = p.parse_args()
base = Path(tempfile.mkdtemp(prefix='ctlst-lock-swipe-'))
runtime = base / 'runtime'
runtime.mkdir(mode=0o700)
env = os.environ | {'XDG_RUNTIME_DIR': str(runtime), 'XDG_CONFIG_HOME': str(base / 'config'),
    'CTLST_GENERATED_DIR': str(base), 'WLR_BACKENDS': 'headless,libinput',
    'LIBSEAT_BACKEND': 'seatd', 'WLR_RENDERER': 'pixman', 'GSK_RENDERER': 'cairo', 'GTK_A11Y': 'none'}
env.pop('SWAYSOCK', None)
env.pop('WAYLAND_DISPLAY', None)
(base / 'sway.conf').write_text('output HEADLESS-1 mode 960x1920 scale 2\noutput * bg "#ff00ff" solid_color\nxwayland disable\n')
sway = touch = proc = None
marker = runtime / 'ctlstlock.visible'

def run(*args):
    return subprocess.check_output(args, env=env, text=True, stderr=subprocess.STDOUT, timeout=10)

def wait(label, predicate):
    until = time.monotonic() + 6
    while time.monotonic() < until:
        if predicate(): return
        time.sleep(.02)
    raise AssertionError(label)

def capture(name, width, height):
    path = base / (name + '.png')
    run('grim', str(path))
    picture = GdkPixbuf.Pixbuf.new_from_file(str(path))
    x, y = picture.get_width()//2, picture.get_height()-10
    offset = y*picture.get_rowstride() + x*picture.get_n_channels()
    return tuple(picture.get_pixels()[offset:offset+3])

def swipe(width, height, x1, y1, x2, y2, duration=350, midpoint=False):
    coords = [round(x1/width*1079), round(y1/height*2159), round(x2/width*1079), round(y2/height*2159)]
    touch.stdin.write(str(duration) + ' ' + ' '.join(map(str,coords)) + '\n')
    touch.stdin.flush()
    if midpoint:
        time.sleep(duration / 2500)
        assert marker.exists(), 'Dismissed before release'
        assert capture(f'{width}-dragging', width, height) == (255,0,255), 'Background did not travel with sheet'
    assert select.select([touch.stdout], [], [], 5)[0]
    assert touch.stdout.readline().strip() == 'DONE'

try:
    with (base / 'sway.log').open('w') as log:
        sway = subprocess.Popen(['sway','-c',str(base/'sway.conf')], env=env, stdout=log, stderr=log)
    wait('Sway socket', lambda: list(runtime.glob('sway-ipc.*.sock')))
    env['SWAYSOCK'] = str(next(runtime.glob('sway-ipc.*.sock')))
    wait('Wayland', lambda: list(runtime.glob('wayland-*.lock')))
    env['WAYLAND_DISPLAY'] = next(runtime.glob('wayland-*.lock')).name.removesuffix('.lock')
    touch = subprocess.Popen([a.touch_inject,'--stdin','1080','2160','3500'], env=env,
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
    assert select.select([touch.stdout], [], [], 5)[0]
    assert touch.stdout.readline().strip() == 'READY'
    time.sleep(.4)
    for width,height,method in ((480,960,'touch'),(640,320,'touch'),(480,960,'pointer'),(640,320,'keyboard')):
        run('swaymsg',f'output HEADLESS-1 mode {width*2}x{height*2} scale 2')
        with (base/f'{width}-{method}.log').open('w') as log:
            proc = subprocess.Popen([a.binary], env=env, stdout=log, stderr=log)
        control = runtime / 'ctlst-lock.sock'
        wait('lock socket', control.is_socket)
        with socket.socket(socket.AF_UNIX,socket.SOCK_DGRAM) as client:
            client.sendto(b'L',str(control))
        wait('visible privacy sheet',marker.exists)
        time.sleep(.25)
        assert capture(f'{width}-{method}-covered',width,height) != (255,0,255)
        if method == 'touch':
            swipe(width,height,20,height*.7,20,height*.7-35)
            time.sleep(.3)
            assert marker.exists() and capture(f'{width}-snapback',width,height) != (255,0,255)
            swipe(width,height,20,height*.7,160,height*.7)
            time.sleep(.3)
            assert marker.exists(), 'Horizontal gesture dismissed'
            # Deliberately begin over Continue, not just the painted canvas.
            swipe(width,height,width/2,height-78,width/2,max(20,height-320),1200,True)
        elif method == 'pointer':
            run('wlrctl','pointer','move','-10000','-10000')
            run('wlrctl','pointer','move',str(width//2),str(height-78))
            run('wlrctl','pointer','click')
        else:
            run('wtype','-k','space')
        wait('privacy release',lambda: not marker.exists())
        assert proc.wait(timeout=4) == 0
        proc = None
        assert capture(f'{width}-{method}-dismissed',width,height) == (255,0,255)
        print(f'PASS: {width}x{height} {method}, whole-sheet travel and release',flush=True)
finally:
    for process in (proc,touch,sway):
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=5)
    print('Evidence:',base,flush=True)
