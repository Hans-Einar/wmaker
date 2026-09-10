#!/usr/bin/env python3
"""Exercise NoWindowOverClip in isolated Xvfb sessions (requires cc and Xlib).

Run from the configured, built repository: python3 tests/clip-maximize.py
No user configuration or running desktop is touched.
"""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
PROBE = r'''
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(int argc, char **argv) {
    Display *d = XOpenDisplay(NULL);
    if (!d) return 2;
    Window root = DefaultRootWindow(d), child;
    if (argc > 1) {
        XEvent ws = {0};
        ws.xclient.type = ClientMessage;
        ws.xclient.window = root;
        ws.xclient.message_type = XInternAtom(d, "_NET_CURRENT_DESKTOP", False);
        ws.xclient.format = 32;
        ws.xclient.data.l[0] = atoi(argv[1]);
        XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &ws);
        XFlush(d);
        usleep(300000);
    }
    Window w = XCreateSimpleWindow(d, root, 200, 200, 200, 120, 0, 0, 0);
    XStoreName(d, w, "Clip maximization test");
    XMapWindow(d, w);
    XFlush(d);
    usleep(300000);
    XEvent e = {0};
    e.xclient.type = ClientMessage;
    e.xclient.window = w;
    e.xclient.message_type = XInternAtom(d, "_NET_WM_STATE", False);
    e.xclient.format = 32;
    e.xclient.data.l[0] = 1;
    e.xclient.data.l[1] = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_VERT", False);
    e.xclient.data.l[2] = XInternAtom(d, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
    XSendEvent(d, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &e);
    XFlush(d);
    usleep(300000);
    XWindowAttributes a;
    int x, y;
    XGetWindowAttributes(d, w, &a);
    XTranslateCoordinates(d, w, root, 0, 0, &x, &y, &child);
    printf("%d %d %d %d\n", x, y, a.width, a.height);
    XDestroyWindow(d, w);
    XCloseDisplay(d);
    return 0;
}
'''


def stop(process):
    if process is not None:
        try:
            os.killpg(process.pid, signal.SIGTERM)
        except ProcessLookupError:
            pass
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()


with tempfile.TemporaryDirectory(prefix="wmaker-clip-test-") as tmp:
    tmp = Path(tmp)
    (tmp / "probe.c").write_text(PROBE)
    subprocess.run(["cc", str(tmp / "probe.c"), "-lX11", "-o", str(tmp / "probe")], check=True)
    xvfb = None
    try:
        cases = [
            ("disabled", "NO", (4, 0), (0, 5), False, 0, False),
            ("left", "YES", (4, 0), (0, 5), False, 0, False),
            ("right", "YES", (732, 0), (0, 5), False, 0, False),
            ("top", "YES", (0, 4), (5, 0), False, 0, False),
            ("bottom", "YES", (0, 532), (5, 0), False, 0, False),
            ("collapsed", "YES", (4, 0), (3, 5), True, 0, False),
            ("workspace", "YES", (4, 0), (3, 5), False, 1, False),
            ("omnipresent", "YES", (4, 0), (3, 5), False, 1, True),
            ("full-maximize", "YES", (4, 0), (0, 5), False, 0, False),
            ("no-clip", "YES", (4, 0), (0, 5), False, 0, False),
        ]
        for name, enabled, pos, iconpos, collapsed, workspace, omni in cases:
            # Xvfb chooses an unused display; never connect the test WM to the user's X server.
            read_fd, write_fd = os.pipe()
            xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "800x600x24", "-nolisten", "tcp"],
                                    pass_fds=(write_fd,), start_new_session=True, stderr=subprocess.DEVNULL)
            os.close(write_fd)
            with os.fdopen(read_fd) as pipe:
                display = ":" + pipe.readline().strip()
            assert display != ":", "Xvfb did not start"
            profile = tmp / name
            defaults = profile / "Defaults"
            defaults.mkdir(parents=True)
            if name == "full-maximize":
                (defaults / "WMWindowAttributes").write_text('{ "*" = { FullMaximize = YES; }; }')
            (defaults / "WindowMaker").write_text(
                "{ NoWindowOverDock = NO; NoWindowOverIcons = NO; "
                f"NoWindowOverClip = {enabled}; "
                "IconSize = 64; SaveSessionOnExit = NO; DisableAnimations = YES; "
                "WorkspaceBorderSize = 0; }"
            )
            (defaults / "WMState").write_text('''{
                Clip = { Position = "%d,%d"; Name = Logo.WMClip; Command = "-"; };
                Workspaces = (
                    { Name = One; Clip = { Collapsed = %s; AutoCollapse = No;
                        Applications = ({Name = test.Test; Command = true;
                            Position = "%d,%d"; Omnipresent = %s; AutoLaunch = No;}); }; },
                    { Name = Two; Clip = { Collapsed = No; Applications = (); }; }
                ); Applications = ();
            }''' % (*pos, "Yes" if collapsed else "No", *iconpos, "Yes" if omni else "No"))
            env = dict(os.environ, DISPLAY=display, WMAKER_USER_ROOT=str(profile))
            wm = None
            with (profile / "wm.log").open("w+") as log:
                try:
                    args = [str(ROOT / "src/wmaker"), "--no-dock", "--no-autolaunch"]
                    if name == "no-clip":
                        args.append("--no-clip")
                    wm = subprocess.Popen(args,
                                          env=env, stdout=log, stderr=log, start_new_session=True)
                    time.sleep(1)
                    assert wm.poll() is None, "Window Maker exited"
                    x, y, w, h = map(int, subprocess.check_output([str(tmp / "probe"), str(workspace)], env=env, text=True).split())
                    print(name, (x, y, w, h), flush=True)
                    if name in ("disabled", "full-maximize", "no-clip"):
                        assert x < 4 and w > 790
                    elif name in ("left", "collapsed", "workspace"):
                        assert 72 <= x <= 74 and w > 720
                    elif name == "right":
                        assert x < 4 and 720 < x + w <= 728
                    elif name == "top":
                        assert y >= 72 and h > 490
                    elif name == "bottom":
                        assert y < 40 and 490 < y + h <= 528
                    elif name == "omnipresent":
                        assert x >= 264 and w > 530
                except Exception:
                    log.seek(0)
                    print(log.read())
                    raise
                finally:
                    stop(wm)
                    stop(xvfb)
                    xvfb = None
        print("All Clip maximization checks passed.")
    finally:
        stop(xvfb)
