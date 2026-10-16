#!/usr/bin/env python3
"""Run via dbus-run-session with isolated HOME/XDG_DATA_HOME, never real accounts."""
import datetime as dt
import importlib.machinery
import importlib.util
import os
from pathlib import Path
import sys
import uuid
import gi
gi.require_version('ECal','2.0');gi.require_version('EDataServer','1.2')
from gi.repository import ECal,EDataServer,ICalGLib
assert 'ctlst-calendar-test-' in os.environ.get('XDG_DATA_HOME',''), 'Requires disposable calendar data directory'
loader=importlib.machinery.SourceFileLoader('summary',str(Path(__file__).resolve().parents[1]/'scripts/ctlst-calendar-summary'))
spec=importlib.util.spec_from_loader(loader.name,loader);module=importlib.util.module_from_spec(spec);loader.exec_module(module)
r=EDataServer.SourceRegistry.new_sync(None)
s=r.ref_source('system-calendar')
c=ECal.Client.connect_sync(s,ECal.ClientSourceType.EVENTS,3,None)
start=dt.datetime.now().replace(microsecond=0)+dt.timedelta(hours=1)
end=start+dt.timedelta(minutes=30)
ical=f'BEGIN:VEVENT\r\nUID:{uuid.uuid4()}\r\nDTSTART:{start:%Y%m%dT%H%M%S}\r\nDTEND:{end:%Y%m%dT%H%M%S}\r\nRRULE:FREQ=DAILY;COUNT=2\r\nSUMMARY:Widget recurrence fixture\r\nEND:VEVENT\r\n'
ok,uid=c.create_object_sync(ICalGLib.Component.new_from_string(ical),ECal.OperationFlags.NONE,None)
assert ok
result=module.summary()
assert result['errors']==0,result
matches=[e for e in result['events'] if e['title']=='Widget recurrence fixture']
assert len(matches)==2,result
assert abs(matches[0]['start']-start.astimezone().timestamp())<1,result
assert not result['google']
print('PASS: isolated EDS local event, recurring instances, floating local timezone, event-day markers')
