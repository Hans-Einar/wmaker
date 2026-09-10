# SPDX-License-Identifier: GPL-2.0-or-later
"""Three-finger control for the Window Maker fork. Uses physical contact sequences."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import time
from engine import Motion, Preview
from x11 import Connection

class Controller:
    def __init__(self,connection,args):
        self.connection=connection;self.args=args;self.motion=Motion()
        self.preview=Preview();p=self.preview
        p.travel=args.swipe_length;p.coefficient=args.coefficient;p.threshold=args.lock_threshold
        p.curtain_travel=args.curtain_height;p.filtered=True;p.wrap=args.wrap
        self.count=0;self.sequence=False;self.blocked=False;self.target=0;self.shaded=False
        self.current_desktop=1;self.paused_history=False
    def packet(self,packet):
        if 'error' in packet:raise RuntimeError(packet['error'])
        if 'device' in packet:
            width,height=packet.get('width_mm'),packet.get('height_mm')
            if not width or not height:raise RuntimeError('Touchpad must report physical axis resolution')
            self.motion.width=width;self.motion.height=height
            print(f"Touchpad: {packet['device']} ({width:.1f} × {height:.1f} mm)",flush=True)
        if packet.get('reset'):
            self.paused_history=True;return  # lost data is not evidence of finger-up
        if 'contacts' not in packet:return
        contacts={p['id']:(p['x'],p['y']) for p in packet['contacts']}
        if self.paused_history:
            self.motion.previous=dict(contacts);self.paused_history=False
        _,dx,_,dy=self.motion.update(contacts)
        previous=self.count;self.count=len(contacts)
        if not self.count:
            self.preview.release();self.sequence=False;self.blocked=False;self.target=0;return
        if self.blocked or self.count!=3:return
        if not self.sequence:
            state=self.connection.request()
            if not state or not state[1]:self.blocked=True;return
            _,_,workspace,count,target,shaded,_=state
            if count<1:self.blocked=True;return
            self.preview.configure_bounds(min(count,self.args.max_desktops),self.args.wrap)
            # Do not jump into the configured range if the user is outside it.
            if workspace>=self.preview.max_desktops:self.blocked=True;return
            self.preview.desktop=workspace+1;self.current_desktop=workspace+1
            self.shaded=bool(shaded);self.target=target
            self.preview.curtain=0. if shaded else 1.
            self.preview.begin(3,time.monotonic());self.sequence=True;return
        if previous!=3:return  # rebase when a finger joins/leaves
        self.preview.update(3,dx,dy,time.monotonic())
        if self.preview.axis=='x' and self.preview.desktop!=self.current_desktop:
            self.perform(1,self.preview.desktop-1,0)
            self.current_desktop=self.preview.desktop
        elif self.preview.axis=='y' and self.target:
            # Use a half-curtain threshold, retaining the previous state at a tie.
            desired=self.shaded
            if self.preview.curtain<.5-1e-9:desired=True
            elif self.preview.curtain>.5+1e-9:desired=False
            if desired!=self.shaded:
                self.perform(2,int(desired),self.target);self.shaded=desired
    def perform(self,operation,value,target):
        if self.args.verbose or self.args.dry_run:
            print(f"{'Would do' if self.args.dry_run else 'Action'}: {'workspace' if operation==1 else 'shade'} {value+1 if operation==1 else value}",flush=True)
        if not self.args.dry_run:
            state=self.connection.request(operation,value,target)
            if not state or not state[1]:self.blocked=True


def arguments():
    p=argparse.ArgumentParser(prog='wmtouchpad',description=__doc__)
    p.add_argument('--swipe-length',type=float,default=15,help='mm of weighted motion per workspace (default 15)')
    p.add_argument('--coefficient',type=float,default=.5,help='acceleration C in dx * max(1, abs(dx)*C) (default .5)')
    p.add_argument('--lock-threshold',type=float,default=5,help='mm before the axis locks (default 5)')
    p.add_argument('--curtain-height',type=float,default=40,help='mm of travel for shade/unshade (default 40)')
    p.add_argument('--max-desktops',type=int,default=10,help='upper limit; never creates workspaces (default 10)')
    p.add_argument('--wrap',action='store_true',help='wrap at workspace limits (default off)')
    p.add_argument('--dry-run',action='store_true',help='report intended actions without performing them')
    p.add_argument('--verbose',action='store_true')
    p.add_argument('--check',action='store_true',help='query the running WM and exit')
    p.add_argument('--replay',type=Path,help='read saved synthetic contact frames instead of a device')
    a=p.parse_args()
    if not (0<a.swipe_length<=1000 and 0<=a.coefficient<=20 and 0<a.lock_threshold<=100 and 0<a.curtain_height<=1000 and 1<=a.max_desktops<=100):
        p.error('invalid tuning values')
    return a

def main():
    args=arguments();connection=Connection();reader=None;running=True
    def stop(*_):
        nonlocal running
        running=False
    signal.signal(signal.SIGTERM,stop);signal.signal(signal.SIGINT,stop)
    try:
        if args.check:
            state=connection.request()
            print(json.dumps({'protocol':state[0] if state else None,'enabled':bool(state and state[1]),'workspace':state[2]+1 if state else None,'workspaces':state[3] if state else None}))
            return 0 if state and state[1] else 1
        runtime=Path(os.environ.get('XDG_RUNTIME_DIR',f'/run/user/{os.getuid()}'))
        display=os.environ.get('DISPLAY','default').replace('/','_')
        lock=(runtime/f'wmtouchpad-{display}.lock').open('w')
        try:fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except BlockingIOError:raise RuntimeError('wmtouchpad is already running on this display')
        waiting=False
        while running:
            state=connection.request()
            if state and state[1]:break
            if not waiting:
                print('Waiting for Window Maker with TouchpadGestures=YES (restart WM from its menu after installing).',flush=True);waiting=True
            if args.replay:raise RuntimeError('Replay requires an enabled WM')
            for _ in range(10):
                if not running:break
                time.sleep(.1)
        if not running:return 0
        controller=Controller(connection,args)
        if args.replay:
            for line in args.replay.read_text().splitlines():
                if not running:break
                controller.packet(json.loads(line))
            return 0
        # The installed reader opens only a touchpad and then drops root privileges.
        reader=subprocess.Popen(['sudo','-n','/usr/bin/python3',str(Path(__file__).with_name('reader.py'))],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=None,bufsize=0)
        buffer=b''
        while running:
            ready,_,_=select.select([reader.stdout],[],[],.2)
            if not ready:continue
            data=os.read(reader.stdout.fileno(),65536)
            if not data:raise RuntimeError('Touchpad reader stopped')
            buffer+=data
            while b'\n' in buffer:
                line,buffer=buffer.split(b'\n',1)
                if line:controller.packet(json.loads(line))
    finally:
        if reader:
            reader.stdin.close()
            try:reader.wait(timeout=2)
            except subprocess.TimeoutExpired:reader.terminate();reader.wait(timeout=2)
        connection.close()
    return 0

if __name__=='__main__':
    try:sys.exit(main())
    except (RuntimeError,OSError,ValueError) as error:
        print(f'wmtouchpad: {error}',file=sys.stderr);sys.exit(1)
