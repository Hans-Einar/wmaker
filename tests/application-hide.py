#!/usr/bin/env python3
"""Exercise minimization and icon activation in private Xvfb/Window Maker sessions."""
import ctypes as C
from ctypes.util import find_library
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
CLIENT = r'''
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static void state(Display *d, Window w) {
    Atom type; int format; unsigned long n, left; unsigned char *data = NULL;
    XGetWindowProperty(d,w,XInternAtom(d,"WM_STATE",False),0,2,False,AnyPropertyType,
                       &type,&format,&n,&left,&data);
    if (n >= 2) printf("%lu %lu ", ((unsigned long*)data)[0], ((unsigned long*)data)[1]);
    else printf("0 0 ");
    if(data) XFree(data);
}
int main(int argc, char **argv) {
    Display *d=XOpenDisplay(NULL); if(!d) return 1;
    Window root=DefaultRootWindow(d);
    Window leader=XCreateSimpleWindow(d,root,0,0,1,1,0,0,0);
    Window windows[2], cover=None;
    XClassHint cls={"probe","Probe"};
    XSetClassHint(d,leader,&cls);
    for(int i=0;i<2;i++) {
        windows[i]=XCreateSimpleWindow(d,root,100+i*250,130,220,160,0,0,0x448888);
        XSetClassHint(d,windows[i],&cls);
        XStoreName(d,windows[i],i ? "Second probe" : "First probe");
        if(argc < 2 || strcmp(argv[1],"ungrouped")) {
            Atom atom=XInternAtom(d,"WM_CLIENT_LEADER",False);
            XChangeProperty(d,windows[i],atom,XA_WINDOW,32,PropModeReplace,(unsigned char*)&leader,1);
            XWMHints h={.flags=WindowGroupHint,.window_group=leader};
            XSetWMHints(d,windows[i],&h);
        }
        XMapWindow(d,windows[i]);
    }
    XFlush(d); puts("ready"); fflush(stdout);
    char cmd[40];
    while(fgets(cmd,sizeof(cmd),stdin)) {
        if(cmd[0]=='q') break;
        if(cmd[0]=='b') {
            cover=XCreateSimpleWindow(d,root,80,100,600,300,0,0,0x884444);
            XClassHint c={"cover","Cover"};XSetClassHint(d,cover,&c);
            XMapWindow(d,cover);XFlush(d);
        }
        if(cmd[0]=='t') {
            Window frames[3]={windows[0],windows[1],cover};
            for(int i=0;i<3;i++) {
                Window r,p,*children;unsigned int n;
                for(;;) {
                    XQueryTree(d,frames[i],&r,&p,&children,&n);if(children) XFree(children);
                    if(p==root) break;
                    frames[i]=p;
                }
            }
            Window r,p,*children;unsigned int n;int rank[3]={-1,-1,-1};
            XQueryTree(d,root,&r,&p,&children,&n);
            for(unsigned int j=0;j<n;j++) for(int i=0;i<3;i++)
                if(children[j]==frames[i]) rank[i]=j;
            XFree(children);
            printf("%d\n",rank[0]>rank[2] && rank[1]>rank[2]);fflush(stdout);
        }
        if(cmd[0]=='i') { XIconifyWindow(d,windows[0],DefaultScreen(d)); XFlush(d); }
        if(cmd[0]=='u') { XUnmapWindow(d,strtoul(cmd+1,NULL,10)); XFlush(d); }
        if(cmd[0]=='f') {
            XEvent e={0};e.xclient.type=ClientMessage;e.xclient.window=windows[0];
            e.xclient.message_type=XInternAtom(d,"_NET_ACTIVE_WINDOW",False);
            e.xclient.format=32;e.xclient.data.l[0]=2;
            XSendEvent(d,root,False,SubstructureRedirectMask|SubstructureNotifyMask,&e);XFlush(d);
        }
        if(cmd[0]=='h') {
            KeyCode alt=XKeysymToKeycode(d,XK_Alt_L), h=XKeysymToKeycode(d,XK_h);
            XTestFakeKeyEvent(d,alt,True,0);XTestFakeKeyEvent(d,h,True,0);
            XTestFakeKeyEvent(d,h,False,0);XTestFakeKeyEvent(d,alt,False,0);XFlush(d);
        }
        if(cmd[0]=='w') {
            XEvent e={0};e.xclient.type=ClientMessage;e.xclient.window=root;
            e.xclient.message_type=XInternAtom(d,"_NET_CURRENT_DESKTOP",False);
            e.xclient.format=32;e.xclient.data.l[0]=atoi(cmd+1);
            XSendEvent(d,root,False,SubstructureRedirectMask|SubstructureNotifyMask,&e);XFlush(d);
        }
        if(cmd[0]=='d') {
            Atom t;int f;unsigned long n,left;unsigned char *data=NULL;
            XGetWindowProperty(d,root,XInternAtom(d,"_NET_CURRENT_DESKTOP",False),0,1,False,
                               XA_CARDINAL,&t,&f,&n,&left,&data);
            printf("%lu\n",n ? *(unsigned long*)data : 999UL);fflush(stdout);
            if(data) XFree(data);
        }
        if(cmd[0]=='k') {
            Window tr, parent, *children; unsigned int count;
            XQueryTree(d,root,&tr,&parent,&children,&count);
            for(unsigned int i=0;i<count;i++) {
                XWindowAttributes a; XGetWindowAttributes(d,children[i],&a);
                if(a.width==64 && a.height==64 && a.map_state==IsViewable)
                    printf("%lu,%d,%d ",children[i],a.x,a.y);
            }
            XFree(children); puts(""); fflush(stdout);
        }
        if(cmd[0]=='s') { state(d,windows[0]); state(d,windows[1]); puts(""); fflush(stdout); }
    }
    XCloseDisplay(d); return 0;
}
'''.replace('#include <X11/Xutil.h>', '#include <X11/Xutil.h>\n#include <X11/Xatom.h>')

with tempfile.TemporaryDirectory(prefix='wmaker-hide-test-') as temp:
    temp=Path(temp)
    (temp/'client.c').write_text(CLIENT)
    subprocess.run(['cc',str(temp/'client.c'),'-lX11','-lXtst','-o',str(temp/'client')],check=True)
    for case in sys.argv[1:] or ['legacy','application','clip','dock','ungrouped','no-appicon','no-emulation','hide-key','mini-key','unmapped-icon']:
        rd,wr=os.pipe()
        xv=subprocess.Popen(['Xvfb','-displayfd',str(wr),'-screen','0','800x600x24','-nolisten','tcp'],
                            pass_fds=(wr,),stderr=subprocess.DEVNULL)
        os.close(wr)
        with os.fdopen(rd) as pipe: display=':'+pipe.readline().strip()
        profile=temp/case
        defaults=profile/'Defaults'; defaults.mkdir(parents=True)
        enabled='NO' if case=='legacy' else 'YES'
        (defaults/'WindowMaker').write_text('{MinimizeHidesApplication='+enabled+'; AppIconTogglesHide='+enabled+
            '; SingleClickLaunch=YES; SaveSessionOnExit=NO; DisableAnimations=YES; DoubleClickTime=250;'+
            ('HideKey=None; MiniaturizeKey="Mod1+h";' if case=='mini-key' else 'HideKey="Mod1+h";')+'}')
        if case=='no-emulation':
            (defaults/'WMWindowAttributes').write_text('{Probe={EmulateAppIcon=No;};}')
        if case=='no-appicon':
            (defaults/'WMWindowAttributes').write_text('{Probe={NoAppIcon=Yes;};}')
        if case=='clip':
            (defaults/'WMState').write_text('''{Clip={Name=Logo.WMClip;Position="0,0";Command="-";};
                Workspaces=({Name=One;Clip={Collapsed=No;Applications=({Name=probe.Probe;Command="/bin/true";
                Position="0,1";AutoLaunch=No;Omnipresent=Yes;});};},{Name=Two;});Applications=();}''')
        if case=='dock':
            (defaults/'WMState').write_text('''{Dock={Position="736,0";Applications=(
                {Name=Logo.WMDock;Command="-";Position="0,0";},
                {Name=probe.Probe;Command="/bin/true";Position="0,1";AutoLaunch=No;});}; Applications=();}''')
        env=dict(os.environ,DISPLAY=display,WMAKER_USER_ROOT=str(profile))
        wm=client=None
        log=(profile/'wm.log').open('w+')
        try:
            wm=subprocess.Popen([str(ROOT/'src/wmaker'),'--no-autolaunch'],env=env,stdout=log,stderr=log)
            time.sleep(.8)
            client=subprocess.Popen([str(temp/'client'),'ungrouped' if case in ('ungrouped','no-emulation') else case],env=env,stdin=subprocess.PIPE,
                                    stdout=subprocess.PIPE,text=True)
            assert client.stdout.readline().strip()=='ready'
            time.sleep(.6)
            def command(cmd):
                client.stdin.write(cmd+'\n');client.stdin.flush()
            def status():
                command('s');return list(map(int,client.stdout.readline().split()))
            def icons():
                command('k');return [tuple(map(int,item.split(','))) for item in client.stdout.readline().split()]
            if case=='unmapped-icon':
                appicon=sorted((x,i) for i,x,y in icons() if y>400)[0][1]
                command('u'+str(appicon));time.sleep(.2)
            before = icons()
            assert status()[::2]==[1,1]
            command('f');time.sleep(.3)
            command('i' if case in ('legacy','application','clip','dock') else 'h');time.sleep(.5)
            hidden=status()
            if case in ('legacy','ungrouped','no-appicon','no-emulation','unmapped-icon'):
                assert hidden[::2]==[3,1],(case,hidden)
            else:
                assert hidden[::2]==[3,3] and hidden[1]==hidden[3],(case,hidden)
            if case in ('legacy','ungrouped','no-appicon','no-emulation','unmapped-icon'):
                icon=next(i for i,x,y in icons() if i not in {item[0] for item in before})
            if case not in ('legacy','ungrouped','no-appicon','no-emulation','unmapped-icon'):
                assert len(icons())==len(before),(case,'extra miniature icon',before,icons())
            if case=='clip':
                icon=next(i for i,x,y in before if x==0 and y==64)
            elif case=='dock':
                icon=next(i for i,x,y in before if x==736 and y==64)
            elif case not in ('legacy','ungrouped','no-appicon','no-emulation','unmapped-icon'):
                icon=sorted((x,i) for i,x,y in before if y>400)[0][1]
            x=C.CDLL(find_library('X11'));xt=C.CDLL(find_library('Xtst'))
            x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=C.c_void_p
            d=x.XOpenDisplay(display.encode())
            x.XDefaultRootWindow.argtypes=[C.c_void_p];x.XDefaultRootWindow.restype=C.c_ulong
            x.XTranslateCoordinates.argtypes=[C.c_void_p,C.c_ulong,C.c_ulong,C.c_int,C.c_int,
                C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_ulong)]
            x.XFlush.argtypes=[C.c_void_p];x.XCloseDisplay.argtypes=[C.c_void_p]
            xt.XTestFakeMotionEvent.argtypes=[C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_ulong]
            xt.XTestFakeButtonEvent.argtypes=[C.c_void_p,C.c_uint,C.c_int,C.c_ulong]
            px,py,child=C.c_int(),C.c_int(),C.c_ulong()
            x.XTranslateCoordinates(d,icon,x.XDefaultRootWindow(d),32,32,C.byref(px),C.byref(py),C.byref(child))
            def click(delay=.5):
                xt.XTestFakeMotionEvent(d,-1,px.value,py.value,0)
                xt.XTestFakeButtonEvent(d,1,1,0);xt.XTestFakeButtonEvent(d,1,0,0)
                x.XFlush(d);time.sleep(delay)
            if case in ('legacy','ungrouped','no-appicon','no-emulation','unmapped-icon'):
                click(.06);click()
                assert status()[::2]==[1,1],(case,'restore miniature',status())
                x.XCloseDisplay(d)
                print('PASS',case,'miniaturizes one window and restores from miniature',flush=True)
                continue
            click();assert status()[::2]==[1,1],(case,'unhide',status())
            click();assert status()[::2]==hidden[::2],(case,'hide',status())
            click();assert status()[::2]==[1,1]
            # A double-click in single-click mode must hide only once.
            click(.06);click();assert status()[::2]==hidden[::2],(case,'double click',status())
            click();assert status()[::2]==[1,1]
            if case=='clip':
                command('b');time.sleep(.4)  # cover the app before leaving its workspace
                command('w1');time.sleep(.4)
                click()  # visible elsewhere: switch workspace and raise, without hiding
                command('d');assert client.stdout.readline().strip()=='0'
                assert status()[::2]==[1,1]
                command('t');assert client.stdout.readline().strip()=='1','app not raised above cover'
                click()  # already on the app workspace: hide
                assert status()[::2]==[3,3]
                command('w1');time.sleep(.4)
                click()  # hidden: existing unhide returns to the app workspace
                command('d');assert client.stdout.readline().strip()=='0'
                assert status()[::2]==[1,1]
            x.XCloseDisplay(d)
            print('PASS',case,'minimize hides; icon toggles; double-click does not undo hide',flush=True)
        except Exception:
            log.flush();print((profile/'wm.log').read_text());raise
        finally:
            if client and client.poll() is None:
                client.stdin.write('q\n');client.stdin.flush();client.wait(timeout=3)
            if wm and wm.poll() is None: wm.terminate()
            xv.terminate();xv.wait(timeout=5)
            log.close()
