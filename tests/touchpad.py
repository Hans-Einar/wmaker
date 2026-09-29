#!/usr/bin/env python3
"""Real WM IPC + synthetic physical contacts in private Xvfb sessions; no real input."""
import ctypes as C
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'util/touchpad'))
from x11 import Connection
from controller import Controller

def prop(connection,window,name):
    x=connection.x;d=connection.display
    atom=x.XInternAtom(d,name.encode(),0)
    typ=C.c_ulong();fmt=C.c_int();n=C.c_ulong();left=C.c_ulong();data=C.c_void_p()
    x.XGetWindowProperty(d,window,atom,0,128,False,0,C.byref(typ),C.byref(fmt),C.byref(n),C.byref(left),C.byref(data))
    try:return list(C.cast(data,C.POINTER(C.c_ulong*n.value)).contents) if fmt.value==32 and n.value else []
    finally:
        if data:x.XFree(data)

def args(wrap=False):
    return SimpleNamespace(swipe_length=15,coefficient=.5,lock_threshold=5,curtain_height=40,max_desktops=10,wrap=wrap,verbose=False,dry_run=False)

def packet(controller,n,x=0,y=0):
    controller.packet({'contacts':[{'id':i,'x':.2+i*.2+x,'y':.6+y} for i in range(n)]})

def model(connection,wrap=False):
    controller=Controller(connection,args(wrap))
    controller.packet({'device':'Synthetic pad','width_mm':100,'height_mm':60})
    return controller

with tempfile.TemporaryDirectory(prefix='wmaker-touchpad-test-') as tmp:
    tmp=Path(tmp)
    for enabled in (False,True):
        rd,wr=os.pipe()
        server=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','1024x768x24','-nolisten','tcp'],pass_fds=(wr,),stderr=subprocess.DEVNULL)
        os.close(wr)
        with os.fdopen(rd) as stream:display=':'+stream.readline().strip()
        profile=tmp/str(enabled);defaults=profile/'Defaults';defaults.mkdir(parents=True)
        startup = profile / 'Library/WindowMaker/autostart'
        startup.parent.mkdir(parents=True)
        startup.write_text('#!/bin/sh\nexit 0\n')
        startup.chmod(0o755)
        (defaults/'WindowMaker').write_text('{TouchpadGestures='+('YES' if enabled else 'NO')+';SaveSessionOnExit=NO;DisableAnimations=YES;}')
        (defaults/'WMState').write_text('{Workspaces=('+','.join('{Name=W'+str(i)+';}' for i in range(10))+');Applications=();}')
        log=(profile/'wm.log').open('w+')
        wm=subprocess.Popen([str(ROOT/'src/wmaker'),'--for-real','--no-autolaunch'],env=dict(os.environ,DISPLAY=display,WMAKER_USER_ROOT=str(profile)),stdout=log,stderr=log)
        connection=None
        old_display=os.environ.get('DISPLAY');os.environ['DISPLAY']=display
        try:
            connection=Connection();state=None
            deadline=time.monotonic()+8
            while state is None and time.monotonic()<deadline:
                assert wm.poll() is None, 'Window Maker exited during startup'
                state=connection.request()
            assert state and state[1]==enabled and state[3]==10,state
            if not enabled:
                state=connection.request(1,5);assert state[2]==0
                assert connection.request(3,1)[4]==state[4]
                assert connection.request(4,1)[4]==state[4]
                print('PASS disabled preference rejects touchpad actions');continue
            control=model(connection);packet(control,3)
            for i in range(15):packet(control,3,-.01*(i+1))
            assert connection.request()[2]==1
            packet(control,2,.1);packet(control,1,.2)
            assert connection.request()[2]==1
            packet(control,3)
            for i in range(15):packet(control,3,-.01*(i+1))
            assert connection.request()[2]==2
            packet(control,0)
            for n in (1,2):
                packet(control,n)
                for i in range(20):packet(control,n,-.01*(i+1))
                packet(control,0)
            assert connection.request()[2]==2
            assert connection.request(1,999)[2:4]==[2,10]
            connection.request(1,9);control=model(connection);packet(control,3)
            for i in range(20):packet(control,3,-.01*(i+1))
            assert connection.request()[2]==9
            for i in range(10):packet(control,3,-.2+.01*(i+1))
            assert connection.request()[2]==8
            packet(control,0)
            connection.request(1,9);control=model(connection,True);packet(control,3)
            for i in range(15):packet(control,3,-.01*(i+1))
            assert connection.request()[2]==0
            packet(control,0)
            # Create two real clients and verify shade/unshade targets captured at start.
            x=connection.x;d=connection.display
            x.XMapWindow.argtypes=[C.c_void_p,C.c_ulong]
            x.XStoreName.argtypes=[C.c_void_p,C.c_ulong,C.c_char_p]
            window=x.XCreateSimpleWindow(d,connection.root,100,100,240,160,0,0,0x226644)
            x.XStoreName(d,window,b'Touchpad test');x.XMapWindow(d,window);x.XFlush(d);time.sleep(.3)
            assert connection.request()[4]==window
            control=model(connection);packet(control,3)
            for i in range(40):packet(control,3,0,-.01*(i+1))
            shaded=x.XInternAtom(d,b'_NET_WM_STATE_SHADED',0)
            assert shaded in prop(connection,window,'_NET_WM_STATE')
            packet(control,2);packet(control,1)
            assert shaded in prop(connection,window,'_NET_WM_STATE')
            # Focus another window while paused: the original target must be unshaded.
            other=x.XCreateSimpleWindow(d,connection.root,400,100,240,160,0,0,0x442266)
            x.XMapWindow(d,other);x.XFlush(d);time.sleep(.2)
            assert connection.request()[4]==other
            packet(control,3)
            for i in range(20):packet(control,3,0,.01*(i+1))
            assert shaded not in prop(connection,window,'_NET_WM_STATE')
            assert shaded not in prop(connection,other,'_NET_WM_STATE')
            packet(control,0)
            x.XDestroyWindow(d,window);x.XFlush(d);time.sleep(.1)
            assert connection.request(2,1,window) is not None  # stale target is safe
            print('PASS native workspaces, limits/wrap, 3→2→1→3, ignored 1/2 fingers, shade/unshade, stable target, destroyed target')
            # Real modifier state on this private X server only; never real input.
            xt=C.CDLL('libXtst.so.6')
            xt.XTestFakeKeyEvent.argtypes=[C.c_void_p,C.c_uint,C.c_int,C.c_ulong]
            x.XKeysymToKeycode.argtypes=[C.c_void_p,C.c_ulong]
            x.XSync.argtypes=[C.c_void_p,C.c_int]
            def key(symbol,down):
                xt.XTestFakeKeyEvent(d,x.XKeysymToKeycode(d,symbol),down,0);x.XSync(d,False)
            class ClassHint(C.Structure):
                _fields_=[('instance',C.c_char_p),('klass',C.c_char_p)]
            x.XSetClassHint.argtypes=[C.c_void_p,C.c_ulong,C.POINTER(ClassHint)]
            def client(instance,klass):
                win=x.XCreateSimpleWindow(d,connection.root,200,200,240,160,0,0,0x446622)
                hint=ClassHint(instance,klass);x.XSetClassHint(d,win,C.byref(hint))
                x.XMapWindow(d,win);x.XFlush(d);time.sleep(.2)
                assert connection.request()[4]==win
                return win
            kitty1=client(b'one',b'kitty');kitty2=client(b'two',b'kitty')
            # A same-class client on another workspace must never be selected.
            connection.request(1,1);away=client(b'away',b'kitty');connection.request(1,0)
            windows=sorted([other,kitty1,kitty2])
            def focused():
                state=connection.request();assert state[2]==0,state
                active=prop(connection,connection.root,'_NET_ACTIVE_WINDOW')
                assert active==[state[4]],(active,state)
                return state[4]
            def swipe(direction=-1):
                control=model(connection);packet(control,3)
                for i in range(15):packet(control,3,direction*.01*(i+1))
                packet(control,0)
            key(0xffe3,True)  # Control_L
            start=focused();seen=[]
            for _ in range(len(windows)):
                before=focused();swipe()
                assert focused()==windows[(windows.index(before)+1)%len(windows)]
                seen.append(focused())
            assert set(seen)==set(windows) and focused()==start
            swipe(1);assert focused()==windows[(windows.index(start)-1)%len(windows)]
            key(0xffe3,False)
            # One uninterrupted sweep: workspace -> all windows -> current class
            # -> all windows -> another class -> workspace, without mode-change jumps.
            for _ in windows:
                if focused()==kitty2:break
                connection.request(3,1)
            assert focused()==kitty2
            connection.request(1,1)
            control=model(connection);packet(control,3)
            def travel(position,direction=-1):
                for i in range(15):packet(control,3,position+direction*.01*(i+1))
                return position+direction*.15
            position=travel(0,1);assert focused()==kitty2
            def modifier(symbol,down):
                before=connection.request()[2:5]
                key(symbol,down)
                control.update_modifiers()  # also polled while contacts are stationary
                assert connection.request()[2:5]==before
                assert control.preview.axis=='x'
            modifier(0xffe3,True)
            position=travel(position);assert focused()==other
            position=travel(position);assert focused()==kitty1
            modifier(0xffe1,True)
            assert control.cycle_target==kitty1  # captured now, not at finger-down
            position=travel(position);assert focused()==kitty2
            position=travel(position);assert focused()==kitty1
            # A partial finger lift pauses motion, but still accepts modifier changes.
            packet(control,2,position)
            modifier(0xffe1,False)
            packet(control,3,position)
            position=travel(position);assert focused()==kitty2
            position=travel(position);assert focused()==other
            modifier(0xffe1,True)
            assert control.cycle_target==other  # pressing Shift again captures a new type
            position=travel(position);assert focused()==other
            modifier(0xffe1,False)
            # Exercise mode detection directly in a moving contact packet too.
            key(0xffe3,False);packet(control,3,position)
            assert focused()==other and control.cycle_operation==0
            position=travel(position)
            assert connection.request()[2]==1
            packet(control,0);connection.request(1,0)
            # Start on a Kitty, then cycle only its class across separate instances.
            for _ in windows:
                if focused()==kitty2:break
                connection.request(3,1)
            assert focused()==kitty2
            key(0xffe3,True);key(0xffe1,True)  # Shift_L
            swipe();assert focused()==kitty1
            swipe();assert focused()==kitty2
            swipe(1);assert focused()==kitty1
            key(0xffe1,False);key(0xffe3,False)
            # Minimized current-workspace clients are restored and focused.
            x.XIconifyWindow.argtypes=[C.c_void_p,C.c_ulong,C.c_int]
            x.XIconifyWindow(d,kitty2,0);x.XFlush(d);time.sleep(.2)
            assert prop(connection,kitty2,'WM_STATE')[0]==3
            connection.request(4,1,kitty1)
            assert focused()==kitty2 and prop(connection,kitty2,'WM_STATE')[0]==1
            stacking=prop(connection,connection.root,'_NET_CLIENT_LIST_STACKING')
            assert all(stacking.index(kitty2)>stacking.index(w) for w in (other,kitty1)),stacking
            # Closing the class anchor during a gesture must be harmless.
            x.XDestroyWindow(d,kitty1);x.XFlush(d);time.sleep(.1)
            before=focused();connection.request(4,1,kitty1);assert focused()==before
            assert wm.poll() is None
            print('PASS Ctrl cycling/wrap/reverse, live modifier transitions, Shift anchor recapture, Ctrl+Shift class filtering, workspace isolation, minimized restore/raise, stale anchor')
        except Exception:
            log.flush();print((profile/'wm.log').read_text());raise
        finally:
            if connection:connection.close()
            if old_display is None:os.environ.pop('DISPLAY',None)
            else:os.environ['DISPLAY']=old_display
            wm.terminate()
            try:wm.wait(timeout=4)
            except subprocess.TimeoutExpired:wm.kill();wm.wait(timeout=4)
            server.terminate();server.wait(timeout=4);log.close()
