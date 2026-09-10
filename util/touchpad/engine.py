# SPDX-License-Identifier: GPL-2.0-or-later
"""Pure models: gesture preview arbitration and bipolar peak meters."""
import math

class Meter:
    def __init__(self,scale=1):
        self.floor=scale;self.scale=scale;self.value=0.;self.pos=0.;self.neg=0.;self.pt=0.;self.nt=0.;self.time=None;self.scaling_until=0.
    def feed(self,value,now,auto=True):
        self.advance(now);self.value=value
        if auto and abs(value)>self.scale:
            self.scale=max(abs(value),self.floor)
            self.scaling_until=now+.35
        if value>0 and value>=self.pos:self.pos=value;self.pt=now
        if value<0 and value<=self.neg:self.neg=value;self.nt=now
    def advance(self,now):
        dt=0 if self.time is None else max(0,now-self.time);self.time=now
        fall=self.scale*.7*dt
        if now>self.pt+1:self.pos=max(0,self.value,self.pos-fall)
        if now>self.nt+1:self.neg=min(0,self.value,self.neg+fall)

class Preview:
    def __init__(self):
        self.max_desktops=9;self.wrap=False;self.virtual_desktop=5;self.desktop=5;self.position=5.;self.curtain=.7
        self.filtered=False;self.travel=20.;self.coefficient=1.;self.threshold=8.;self.curtain_travel=120.;
        self.axis=None;self.held=None;self.blocked=False;self.active=False;self.last_time=None
        self.x=self.y=self.effective=0.;self.elapsed=0.
    def number(self,index):
        return (index-1)%self.max_desktops+1 if self.wrap else max(1,min(self.max_desktops,index))
    def configure_bounds(self,count,wrap):
        self.release()
        self.max_desktops=max(1,int(count));self.wrap=bool(wrap)
        self.desktop=max(1,min(self.max_desktops,self.desktop))
        self.virtual_desktop=self.desktop;self.position=float(self.desktop)
    def release(self):
        # Only confirmed zero contacts ends a physical contact sequence.
        self.end()
        self.held=None;self.blocked=False
    def begin(self,n,now):
        if self.active or n!=3:return
        self.active=True
        self.axis=self.held if self.filtered else None
        self.x=self.y=self.effective=0.;self.elapsed=0.
        self.virtual_desktop=self.desktop
        self.base=self.desktop;self.base_curtain=self.curtain;self.position=float(self.desktop);self.last_time=now
    def update(self,n,dx,dy,now):
        if not self.active:return
        if n!=3:return  # pause without losing distance or the selected axis
        dt=max(.001,now-self.last_time);self.last_time=now;self.elapsed+=dt
        self.x+=dx;self.y+=dy
        self.effective+=dx*max(1.,abs(dx)*self.coefficient)
        if self.filtered and self.axis is None:
            if max(abs(self.x),abs(self.y))<self.threshold:return
            if max(abs(self.x),abs(self.y))<1.25*min(abs(self.x),abs(self.y)):return
            self.axis='x' if abs(self.x)>abs(self.y) else 'y';self.held=self.axis
        if not self.filtered or self.axis=='x':
            # Each delta is weighted once. Slowing down never revalues past motion.
            offset=self.effective/self.travel
            target=self.base-offset
            self.position=target if self.wrap and self.max_desktops>1 else max(1.,min(self.max_desktops,target))
            if self.position!=target:
                # Drop overshoot at a hard edge so reversing responds immediately.
                self.effective=(self.base-self.position)*self.travel
            # Select the desktop covering more than half the fixed screen.
            # At an exact 50/50 tie retain the previous selection.
            if self.position>self.virtual_desktop+.5+1e-9:
                self.virtual_desktop=math.ceil(self.position-.5)
            elif self.position<self.virtual_desktop-.5-1e-9:
                self.virtual_desktop=math.floor(self.position+.5)
            self.desktop=self.number(self.virtual_desktop)
        if not self.filtered or self.axis=='y':
            self.curtain=max(0,min(1,self.base_curtain+self.y/self.curtain_travel))
    def end(self,cancelled=False):
        # GTK cancellation means its gesture recognizer stopped, not finger-up.
        if not self.active or cancelled:return
        self.position=float(self.desktop);self.virtual_desktop=self.desktop
        self.active=False



class Motion:
    """Mean movement of surviving contacts; adding a finger never jumps the origin."""
    def __init__(self,width=100.,height=100.):
        self.width=width;self.height=height;self.previous={};self.x=self.y=0.
    def update(self,contacts):
        if contacts and not self.previous:self.x=self.y=0.
        common=self.previous.keys() & contacts.keys()
        dx=sum(contacts[i][0]-self.previous[i][0] for i in common)/len(common)*self.width if common else 0.
        dy=sum(contacts[i][1]-self.previous[i][1] for i in common)/len(common)*self.height if common else 0.
        self.x+=dx;self.y+=dy
        self.previous=dict(contacts)
        return self.x,dx,self.y,dy
    def reset(self):self.x=self.y=0.
