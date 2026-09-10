# SPDX-License-Identifier: GPL-2.0-or-later
"""Minimal Xlib request/reply client; no synthetic key presses or global grabs."""
import ctypes as C
import time

class Data(C.Union):
    _fields_=[('b',C.c_char*20),('s',C.c_short*10),('l',C.c_long*5)]
class Message(C.Structure):
    _fields_=[('type',C.c_int),('serial',C.c_ulong),('send_event',C.c_int),('display',C.c_void_p),('window',C.c_ulong),('message_type',C.c_ulong),('format',C.c_int),('data',Data)]
class Event(C.Union):
    _fields_=[('message',Message),('pad',C.c_long*24)]

class Connection:
    def __init__(self):
        self.x=C.CDLL('libX11.so.6');x=self.x
        def fn(name,args,result=C.c_int):
            f=getattr(x,name);f.argtypes=args;f.restype=result;return f
        P=C.c_void_p;U=C.c_ulong;I=C.c_int
        fn('XOpenDisplay',[C.c_char_p],P);fn('XDefaultRootWindow',[P],U)
        fn('XInternAtom',[P,C.c_char_p,I],U)
        fn('XCreateSimpleWindow',[P,U,I,I,C.c_uint,C.c_uint,C.c_uint,U,U],U)
        fn('XSendEvent',[P,U,I,C.c_long,C.POINTER(Event)])
        fn('XGetWindowProperty',[P,U,U,C.c_long,C.c_long,I,U,C.POINTER(U),C.POINTER(I),C.POINTER(U),C.POINTER(U),C.POINTER(P)])
        fn('XFree',[P]);fn('XFlush',[P]);fn('XCloseDisplay',[P]);fn('XDestroyWindow',[P,U])
        self.display=x.XOpenDisplay(None)
        if not self.display:raise RuntimeError('Cannot open X display')
        self.root=x.XDefaultRootWindow(self.display)
        self.window=x.XCreateSimpleWindow(self.display,self.root,0,0,1,1,0,0,0)
        self.command=x.XInternAtom(self.display,b'_WINDOWMAKER_TOUCHPAD',0)
        self.reply=x.XInternAtom(self.display,b'_WINDOWMAKER_TOUCHPAD_REPLY',0)
        self.serial=0
    def request(self,operation=0,value=0,target=0,timeout=.6):
        self.serial+=1
        event=Event();m=event.message;m.type=33;m.display=self.display;m.window=self.window;m.message_type=self.command;m.format=32
        for i,v in enumerate((1,operation,value,target,self.serial)):m.data.l[i]=int(v)
        self.x.XSendEvent(self.display,self.root,False,1<<20,C.byref(event));self.x.XFlush(self.display)
        deadline=time.monotonic()+timeout
        while time.monotonic()<deadline:
            typ=C.c_ulong();fmt=C.c_int();n=C.c_ulong();left=C.c_ulong();data=C.c_void_p()
            self.x.XGetWindowProperty(self.display,self.window,self.reply,0,7,False,6,C.byref(typ),C.byref(fmt),C.byref(n),C.byref(left),C.byref(data))
            try:
                if typ.value==6 and fmt.value==32 and n.value==7:
                    values=list(C.cast(data,C.POINTER(C.c_ulong*7)).contents)
                    if values[0]==1 and values[6]==self.serial:return values
            finally:
                if data:self.x.XFree(data)
            time.sleep(.005)
        return None
    def close(self):
        self.x.XDestroyWindow(self.display,self.window);self.x.XCloseDisplay(self.display)
