#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    Display *display;
    Window root;
    Atom property, command_atom;
    XClientMessageEvent event;
    size_t length;

    if (argc != 2) {
        fprintf(stderr, "usage: %s payload|--save\n", argv[0]);
        return 2;
    }

    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "%s: cannot open display\n", argv[0]);
        return 1;
    }

    root = DefaultRootWindow(display);
    command_atom = XInternAtom(display, "_WINDOWMAKER_COMMAND", False);
    memset(&event, 0, sizeof(event));
    event.type = ClientMessage;
    event.window = root;
    event.message_type = command_atom;
    event.format = 8;
    if (strcmp(argv[1], "--save") == 0) {
        memcpy(event.data.b, "SaveDockState", sizeof("SaveDockState") - 1);
    } else {
        property = XInternAtom(display, "_WINDOWMAKER_DOCK_LAUNCHER", False);
        length = strlen(argv[1]);
        XChangeProperty(display, root, property, XA_STRING, 8, PropModeReplace,
                        (unsigned char *)argv[1], (int)length);
        memcpy(event.data.b, "DockLauncher", sizeof("DockLauncher") - 1);
    }
    XSendEvent(display, root, False, SubstructureRedirectMask,
               (XEvent *)&event);
    XFlush(display);
    XCloseDisplay(display);
    return 0;
}
