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
        (defaults/'WindowMaker').write_text('{TouchpadGestures='+('YES' if enabled else 'NO')+';SaveSessionOnExit=NO;DisableAnimations=YES;}')
        (defaults/'WMState').write_text('{Workspaces=('+','.join('{Name=W'+str(i)+';}' for i in range(10))+');Applications=();}')
        log=(profile/'wm.log').open('w+')
        wm=subprocess.Popen([str(ROOT/'src/wmaker'),'--no-autolaunch'],env=dict(os.environ,DISPLAY=display,WMAKER_USER_ROOT=str(profile)),stdout=log,stderr=log)
        connection=None
        old_display=os.environ.get('DISPLAY');os.environ['DISPLAY']=display
        try:
            time.sleep(.7);connection=Connection();state=connection.request()
            assert state and state[1]==enabled and state[3]==10,state
            if not enabled:
                state=connection.request(1,5);assert state[2]==0
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
        except Exception:
            log.flush();print((profile/'wm.log').read_text());raise
        finally:
            if connection:connection.close()
            if old_display is None:os.environ.pop('DISPLAY',None)
            else:os.environ['DISPLAY']=old_display
            wm.terminate();wm.wait(timeout=4);server.terminate();server.wait(timeout=4);log.close()
