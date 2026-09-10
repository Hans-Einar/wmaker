#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Read only multitouch positions from a touchpad. No grabs, key logging or writes."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import select
import struct
import sys

EVENT=struct.Struct('@llHHi')
ABS_SLOT, ABS_X, ABS_Y, ABS_ID = 0x2f, 0x35, 0x36, 0x39

def ioctl_read(fd, number, size, data=None):
    buf=bytearray(size) if data is None else bytearray(data)
    fcntl.ioctl(fd,0x80000000 | (size<<16) | (ord('E')<<8) | number,buf)
    return buf

def absinfo(fd,code):
    return struct.unpack('6i',ioctl_read(fd,0x40+code,24))

def find_touchpad():
    for entry in sorted(Path('/sys/class/input').glob('event*')):
        name=(entry/'device/name').read_text().strip()
        props=int((entry/'device/properties').read_text().strip().replace(' ',''),16)
        if 'touchpad' in name.lower() and props & 1 and not props & 2:
            return '/dev/input/'+entry.name,name
    raise RuntimeError('No touchpad found')

class Contacts:
    def __init__(self,count,xrange,yrange):
        self.slots=[{'id':-1,'x':0,'y':0} for _ in range(count)]
        self.slot=0;self.xrange=xrange;self.yrange=yrange
    def event(self,code,value):
        if code==ABS_SLOT:self.slot=value
        elif 0<=self.slot<len(self.slots):
            key={ABS_ID:'id',ABS_X:'x',ABS_Y:'y'}.get(code)
            if key:self.slots[self.slot][key]=value
    def frame(self):
        def norm(v,r):return max(0,min(1,(v-r[0])/(r[1]-r[0])))
        return [{'id':s['id'],'x':norm(s['x'],self.xrange),'y':norm(s['y'],self.yrange)} for s in self.slots if s['id']>=0]
    def sync(self,fd):
        self.slot=absinfo(fd,ABS_SLOT)[0]
        count=len(self.slots)
        for code in (ABS_ID,ABS_X,ABS_Y):
            values=struct.unpack(f'{count+1}i',ioctl_read(fd,0x0a,4*(count+1),struct.pack(f'{count+1}i',code,*([0]*count))))
            key={ABS_ID:'id',ABS_X:'x',ABS_Y:'y'}[code]
            for s,value in zip(self.slots,values[1:]):s[key]=value

def emit(value):print(json.dumps(value,separators=(',',':')),flush=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--probe',action='store_true');args=parser.parse_args()
    path,name=find_touchpad()
    fd=os.open(path,os.O_RDONLY|os.O_NONBLOCK|os.O_CLOEXEC)
    # Keep the read-only descriptor, but do not keep root privileges during monitoring.
    if os.geteuid()==0 and 'SUDO_UID' in os.environ:
        os.setgroups([]);os.setgid(int(os.environ['SUDO_GID']));os.setuid(int(os.environ['SUDO_UID']))
    slot=absinfo(fd,ABS_SLOT);x=absinfo(fd,ABS_X);y=absinfo(fd,ABS_Y)
    if slot[1]!=0 or not 0<slot[2]<64 or x[2]<=x[1] or y[2]<=y[1]:raise RuntimeError('Unsupported multitouch ranges')
    state=Contacts(slot[2]+1,(x[1],x[2]),(y[1],y[2]));state.sync(fd)
    ratio=((x[2]-x[1])/x[5])/((y[2]-y[1])/y[5]) if x[5] and y[5] else (x[2]-x[1])/(y[2]-y[1])
    emit({'device':name,'slots':len(state.slots),'ratio':ratio,'width_mm':(x[2]-x[1])/x[5] if x[5] else None,'height_mm':(y[2]-y[1])/y[5] if y[5] else None})
    if args.probe:os.close(fd);return
    emit({'contacts':state.frame()})
    pending=b'';dropped=False
    while True:
        ready,_,_=select.select([fd,sys.stdin.fileno()],[],[])
        if sys.stdin.fileno() in ready and not os.read(sys.stdin.fileno(),1024):break
        if fd not in ready:continue
        data=os.read(fd,EVENT.size*128)
        if not data:break
        pending+=data
        while len(pending)>=EVENT.size:
            _,_,typ,code,value=EVENT.unpack(pending[:EVENT.size]);pending=pending[EVENT.size:]
            if typ==0 and code==3:
                dropped=True;emit({'contacts':[],'reset':True});continue
            if typ==0 and code==0:
                if dropped:state.sync(fd);dropped=False
                emit({'contacts':state.frame()})
            elif typ==3 and not dropped:state.event(code,value)
    os.close(fd)

if __name__=='__main__':
    try:main()
    except BrokenPipeError:pass
    except (OSError,RuntimeError) as exc:
        emit({'error':str(exc)});sys.exit(1)
