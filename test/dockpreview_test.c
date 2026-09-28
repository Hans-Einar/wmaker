/* Integration checks for the temporary dock drawer. Run via dockpreview-xvfb.sh. */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Display *display;
static Window root;
static int anchor_x, right, clip, auto_collapse, persistent_drawer;

static void pause_ms(int ms)
{
	struct timespec delay = { ms / 1000, (ms % 1000) * 1000000L };
	XSync(display, False);
	nanosleep(&delay, NULL);
}

static void check(int condition, const char *message)
{
	if (!condition) {
		fprintf(stderr, "FAIL: %s\n", message);
		exit(1);
	}
	printf("PASS: %s\n", message);
}

static Window drawer(void)
{
	Window parent, returned_root, *children, found = None;
	unsigned int count, i;
	char *name;
	XWindowAttributes attributes;

	XQueryTree(display, root, &returned_root, &parent, &children, &count);
	for (i = 0; i < count; i++) {
		name = NULL;
		if (XFetchName(display, children[i], &name) && name) {
			if (strcmp(name, "Window Maker dock window drawer") == 0 &&
			    XGetWindowAttributes(display, children[i], &attributes) && attributes.map_state == IsViewable)
				found = children[i];
			XFree(name);
		}
	}
	XFree(children);
	return found;
}

static void move(int x, int y)
{
	XTestFakeMotionEvent(display, DefaultScreen(display), x, y, CurrentTime);
	XFlush(display);
}

static void click(unsigned int button)
{
	XTestFakeButtonEvent(display, button, True, CurrentTime);
	XTestFakeButtonEvent(display, button, False, CurrentTime);
	pause_ms(200);
}

static void escape(void)
{
	KeyCode key = XKeysymToKeycode(display, XK_Escape);

	XTestFakeKeyEvent(display, key, True, CurrentTime);
	XTestFakeKeyEvent(display, key, False, CurrentTime);
	pause_ms(300);
}

static unsigned long pixel_at(Window window, int x, int y)
{
	XImage *image = XGetImage(display, window, x, y, 1, 1, AllPlanes, ZPixmap);
	unsigned long pixel = XGetPixel(image, 0, 0) & 0xffffff;

	XDestroyImage(image);
	return pixel;
}

static int grayscale(unsigned long pixel)
{
	int r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;

	return abs(r - g) <= 3 && abs(g - b) <= 3;
}

static void message(Window window, const char *name, long value)
{
	XEvent event = {0};
	event.xclient.type = ClientMessage;
	event.xclient.window = window;
	event.xclient.message_type = XInternAtom(display, name, False);
	event.xclient.format = 32;
	event.xclient.data.l[0] = value;
	event.xclient.data.l[1] = CurrentTime;
	XSendEvent(display, root, False, SubstructureRedirectMask | SubstructureNotifyMask, &event);
	pause_ms(300);
}

static unsigned long property(Window window, const char *name)
{
	Atom type;
	int format;
	unsigned long length, after, result = 0;
	unsigned char *data = NULL;
	XGetWindowProperty(display, window, XInternAtom(display, name, False), 0, 1,
		False, AnyPropertyType, &type, &format, &length, &after, &data);
	if (data && length)
		result = *(unsigned long *)data;
	if (data)
		XFree(data);
	return result;
}

static int stack_position(Window window)
{
	Window returned_root, parent, *children;
	unsigned int count, i;
	int position = -1;

	/* Find the WM frame, then its position in the actual X server stack. */
	do {
		XQueryTree(display, window, &returned_root, &parent, &children, &count);
		XFree(children);
		if (parent != root)
			window = parent;
	} while (parent != root);
	XQueryTree(display, root, &returned_root, &parent, &children, &count);
	for (i = 0; i < count; i++) {
		if (children[i] == window)
			position = i;
	}
	XFree(children);
	return position;
}

static Window client(const char *name, unsigned long color)
{
	XClassHint class = { "previewtest", "PreviewTest" };
	Window window = XCreateSimpleWindow(display, root, 180, 190, 240, 120, 0, 0, color);
	XStoreName(display, window, name);
	{
		Atom close = XInternAtom(display, "WM_DELETE_WINDOW", False);
		XSetWMProtocols(display, window, &close, 1);
	}
	XSetClassHint(display, window, &class);
	XMapWindow(display, window);
	pause_ms(250);
	return window;
}

static void prepare_anchor(void)
{
	if (persistent_drawer) {
		move(anchor_x + (right ? 64 : -64), 96);
		pause_ms(350);
	} else if (clip) {
		move(anchor_x, 32);
		pause_ms(350);
	}
}

static int anchor_visible(void)
{
	Window parent, returned_root, *children;
	unsigned int count, i;
	XWindowAttributes attr;
	int visible = 0;

	XQueryTree(display, root, &returned_root, &parent, &children, &count);
	for (i = 0; i < count; i++) {
		if (XGetWindowAttributes(display, children[i], &attr) &&
		    attr.x == anchor_x - 32 && attr.y == 64 && attr.width == 64 &&
		    attr.height == 64 && attr.map_state == IsViewable)
			visible = 1;
	}
	XFree(children);
	return visible;
}

static Window open_drawer(void)
{
	move(320, 450);
	pause_ms(400);
	prepare_anchor();
	move(anchor_x, 96);
	pause_ms(950);
	check(drawer() != None, "hover opens the drawer");
	return drawer();
}

static void screenshot(const char *path)
{
	XImage *image = XGetImage(display, root, 0, 0, 640, 480, AllPlanes, ZPixmap);
	FILE *file = fopen(path, "wb");
	int x, y;
	if (!file || !image)
		return;
	fprintf(file, "P6\n640 480\n255\n");
	for (y = 0; y < 480; y++) {
		for (x = 0; x < 640; x++) {
			unsigned long pixel = XGetPixel(image, x, y);
			fputc((pixel >> 16) & 255, file);
			fputc((pixel >> 8) & 255, file);
			fputc(pixel & 255, file);
		}
	}
	fclose(file);
	XDestroyImage(image);
}

int main(int argc, char **argv)
{
	Window first, second, other, popup, victim, extra[10];
	XEvent close_event;
	int before_width, close_x, animated, saw_slide, frame, logical;
	XWindowAttributes attr;
	XImage *image;
	unsigned long near_pixel, far_pixel;
	int i, near_x, far_x;

	setvbuf(stdout, NULL, _IOLBF, 0);
	display = XOpenDisplay(NULL);
	if (!display)
		return 2;
	root = DefaultRootWindow(display);
	right = argc > 1 && strcmp(argv[1], "right") == 0;
	animated = argc > 4 && strcmp(argv[4], "effects") == 0;
	clip = argc > 3 && strncmp(argv[3], "clip", 4) == 0;
	persistent_drawer = argc > 3 && strcmp(argv[3], "drawer") == 0;
	auto_collapse = persistent_drawer || (argc > 3 && strcmp(argv[3], "clip-auto") == 0);
	anchor_x = right ? 608 - (clip ? 4 : 0) : 32 + (clip ? 4 : 0);
	if (persistent_drawer)
		anchor_x += right ? -64 : 64;
	move(320, 450);
	first = client("Red one", 0xcc2222);
	second = client("Green two", 0x22cc22);
	message(root, "_NET_CURRENT_DESKTOP", 1);
	other = client("Blue elsewhere", 0x2222cc);
	/* Let the workspace-name overlay fade before caching this window.
	 * Without compositing, obscured windows intentionally use an icon. */
	pause_ms(1000);
	message(root, "_NET_CURRENT_DESKTOP", 0);
	pause_ms(300);

	prepare_anchor();
	move(anchor_x, 96);
	click(Button1);
	check(drawer() != None, "left-click immediately opens a multiwindow drawer");
	popup = drawer();
	click(Button1);
	check(drawer() == popup, "clicking the launcher again keeps its drawer open");
	move(320, 450);
	pause_ms(450);
	check(drawer() != None, "click-opened drawer stays open until dismissed");
	escape();
	check(drawer() == None, "Escape dismisses a click-opened drawer");
	pause_ms(500);

	prepare_anchor();
	move(anchor_x, 96);
	pause_ms(450);
	check(drawer() == None, "no drawer before 700 milliseconds");
	move(320, 450);
	pause_ms(450);
	check(drawer() == None, "leaving cancels pending hover");
	popup = open_drawer();
	XGetWindowAttributes(display, popup, &attr);
	check(attr.width == 3 * 64 + 24 && attr.height == 64, "one tile per window with workspace separator");
	check(attr.x == (right ? anchor_x - 32 - attr.width : anchor_x + 32), "drawer expands toward screen interior");
	near_x = right ? attr.width - 32 : 32;
	far_x = right ? 32 : attr.width - 32;
	image = XGetImage(display, popup, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
	near_pixel = XGetPixel(image, near_x, 25) & 0xffffff;
	far_pixel = XGetPixel(image, far_x, 25) & 0xffffff;
	XDestroyImage(image);
	printf("Snapshot colors: near=%06lx far=%06lx\n", near_pixel, far_pixel);
	if (argc > 2)
		screenshot(argv[2]);
	check(((near_pixel >> 8) & 255) > 180 && (near_pixel & 255) < 60, "current workspace snapshot is nearest the dock");
	check(grayscale(far_pixel), "other workspace snapshot is grayscale");
	check(stack_position(first) < stack_position(second), "background window starts below the focused window");
	move(attr.x + (right ? attr.width - 96 : 96), attr.y + 25);
	pause_ms(250);
	check(stack_position(first) > stack_position(second), "hover temporarily raises the background window");
	check(property(root, "_NET_ACTIVE_WINDOW") == second, "hover preserves keyboard focus");
	move(anchor_x, 96);
	pause_ms(200);
	check(stack_position(first) < stack_position(second), "hover-out restores the original stacking order");
	move(attr.x + (right ? attr.width - 96 : 96), attr.y + 25);
	pause_ms(200);
	click(Button1);
	check(property(root, "_NET_ACTIVE_WINDOW") == first, "click focuses the hovered window");
	move(320, 450);
	pause_ms(400);
	check(stack_position(first) > stack_position(second), "clicked window stays on top after hover-out");
	message(second, "_NET_ACTIVE_WINDOW", 2);
	popup = open_drawer();
	XGetWindowAttributes(display, popup, &attr);
	move(attr.x + far_x, attr.y + 25);
	pause_ms(450);
	check(property(root, "_NET_CURRENT_DESKTOP") == 0, "workspace hover waits 700 milliseconds");
	check(drawer() == popup, "crossing into the drawer keeps it open");
	check(anchor_visible(), "launcher remains visible while using the drawer");
	pause_ms(450);
	check(property(root, "_NET_CURRENT_DESKTOP") == 1, "sustained hover temporarily switches workspace");
	check(drawer() == popup, "workspace browsing preserves the same drawer");
	check(grayscale(pixel_at(popup, near_x, 25)), "original workspace tiles turn grayscale without moving");
	far_pixel = pixel_at(popup, far_x, 25);
	check((far_pixel & 255) > 180 && ((far_pixel >> 8) & 255) < 60,
	      "destination tile regains its original color in the same position");
	/* Workspace changes pump a nested event loop from the hover timer.
	 * Keep hovering after arrival, then exercise the same path again. */
	pause_ms(600);
	move(attr.x + near_x, attr.y + 25);
	pause_ms(1000);
	check(property(root, "_NET_CURRENT_DESKTOP") == 0 && drawer() == popup,
	      "hovering another tile after arrival safely switches back");
	move(attr.x + far_x, attr.y + 25);
	pause_ms(1000);
	check(property(root, "_NET_CURRENT_DESKTOP") == 1 && drawer() == popup,
	      "repeated workspace hover keeps the same drawer alive");
	move(400, 450);
	pause_ms(450);
	check(drawer() == popup, "workspace browsing stays open after hover-out");
	click(Button1);
	check(drawer() == None, "outside click dismisses workspace browsing");
	check(property(root, "_NET_CURRENT_DESKTOP") == 0, "outside click returns to the original workspace");
	check(property(root, "_NET_ACTIVE_WINDOW") == second, "cancelling restores the original focus");

	popup = open_drawer();
	XGetWindowAttributes(display, popup, &attr);
	move(attr.x + far_x, attr.y + 25);
	pause_ms(1000);
	check(property(root, "_NET_CURRENT_DESKTOP") == 1, "workspace browsing can be entered again");
	escape();
	check(drawer() == None && property(root, "_NET_CURRENT_DESKTOP") == 0,
	      "Escape cancels browsing and returns to the original workspace");

	popup = open_drawer();
	XGetWindowAttributes(display, popup, &attr);
	move(attr.x + far_x, attr.y + 25);
	pause_ms(1000);
	click(Button1);
	check(drawer() == None, "selecting a window closes the drawer");
	check(property(root, "_NET_CURRENT_DESKTOP") == 1, "selection commits the previewed workspace");
	check(property(root, "_NET_ACTIVE_WINDOW") == other, "selection focuses the target window");

	/* Minimized windows remain selectable and restore on click. */
	XIconifyWindow(display, other, DefaultScreen(display));
	pause_ms(400);
	popup = open_drawer();
	XGetWindowAttributes(display, popup, &attr);
	move(right ? attr.x + attr.width - 32 : attr.x + 32, attr.y + 25);
	pause_ms(250);
	XGetWindowAttributes(display, other, &attr);
	check(attr.map_state == IsViewable, "hover temporarily reveals a minimized window");
	check(property(other, "WM_STATE") == IconicState, "temporary reveal preserves minimized state");
	move(anchor_x, 96);
	pause_ms(250);
	XGetWindowAttributes(display, other, &attr);
	check(attr.map_state != IsViewable, "hover-out hides the minimized window again");
	XGetWindowAttributes(display, popup, &attr);
	move(right ? attr.x + attr.width - 32 : attr.x + 32, attr.y + 25);
	pause_ms(250);
	click(Button1);
	XGetWindowAttributes(display, other, &attr);
	check(attr.map_state == IsViewable, "click restores a minimized window");

	/* The client decides when a close request is accepted (or cancelled). */
	victim = client("Close request", 0xdd2222);
	popup = open_drawer();
	XGetWindowAttributes(display, popup, &attr);
	before_width = attr.width;
	close_x = right ? attr.width - 10 : 54;
	move(attr.x + (right ? attr.width - 32 : 32), attr.y + 25);
	pause_ms(80);
	check(pixel_at(popup, close_x - 8, 2) == 0xffffff,
	      "hover immediately shows the top-right close button");
	if (argc > 2)
		screenshot(argv[2]);
	XDeleteProperty(display, victim, XInternAtom(display, "WM_PROTOCOLS", False));
	pause_ms(150);
	move(attr.x + close_x, attr.y + 10);
	click(Button1);
	check(!XCheckTypedWindowEvent(display, victim, ClientMessage, &close_event) && drawer() == popup,
	      "unsupported close never force-kills the application");
	{
		Atom close = XInternAtom(display, "WM_DELETE_WINDOW", False);
		XSetWMProtocols(display, victim, &close, 1);
	}
	pause_ms(150);
	click(Button1);
	check(XCheckTypedWindowEvent(display, victim, ClientMessage, &close_event) &&
	      (Atom)close_event.xclient.data.l[0] == XInternAtom(display, "WM_DELETE_WINDOW", False),
	      "close button sends the normal window close protocol");
	XGetWindowAttributes(display, popup, &attr);
	check(drawer() == popup && attr.width == before_width,
	      "a delayed or cancelled close leaves the thumbnail in place");
	XDestroyWindow(display, victim);
	if (animated) {
		saw_slide = 0;
		for (frame = 0; frame < 30; frame++) {
			pause_ms(20);
			if (drawer() != popup)
				break;
			XGetWindowAttributes(display, popup, &attr);
			image = XGetImage(display, popup, 0, 0, attr.width, 64, AllPlanes, ZPixmap);
			for (logical = 0; logical < 128; logical++) {
				unsigned long pixel = XGetPixel(image, right ? attr.width - 1 - logical : logical, 25) & 0xffffff;
				if ((pixel & 255) > 180 && ((pixel >> 8) & 255) < 60 && ((pixel >> 16) & 255) < 60)
					break;
			}
			XDestroyImage(image);
			if (logical > 4 && logical < 63)
				saw_slide = 1;
		}
		check(saw_slide, "animation paints intermediate tile positions while filling the gap");
	}
	pause_ms(700);
	check(drawer() == popup, "closing a window keeps the drawer open");
	XGetWindowAttributes(display, popup, &attr);
	check(attr.width == before_width - 64, "remaining tiles fill the removed tile's gap");
	check((pixel_at(popup, right ? attr.width - 32 : 32, 25) & 255) > 180,
	      "the next window slides toward the launcher without reordering");
	before_width = attr.width;
	/* Red one is the outermost tile, on a different workspace. */
	move(attr.x + (right ? 54 : attr.width - 10), attr.y + 10);
	click(Button1);
	check(XCheckTypedWindowEvent(display, first, ClientMessage, &close_event) &&
	      (Atom)close_event.xclient.data.l[0] == XInternAtom(display, "WM_DELETE_WINDOW", False),
	      "close button targets the correct off-workspace window");
	check(property(root, "_NET_CURRENT_DESKTOP") == 1, "close click does not switch workspace");
	XDestroyWindow(display, first);
	pause_ms(700);
	check(drawer() == popup, "external window closure also keeps the drawer open");
	XGetWindowAttributes(display, popup, &attr);
	check(attr.width == before_width - 64, "external closure removes only its thumbnail");
	escape();
	open_drawer();
	move(320, 450);
	pause_ms(450);
	check(drawer() == None, "leaving the icon and drawer dismisses it");
	if (auto_collapse) {
		pause_ms(300);
		check(!anchor_visible(), "launcher container resumes auto-collapse after the preview closes");
	}

	for (i = 0; i < 10; i++)
		extra[i] = client("Overflow", 0xddaa22);
	popup = open_drawer();
	XGetWindowAttributes(display, popup, &attr);
	check(attr.x >= 0 && attr.x + attr.width <= 640, "overflow stays within the monitor");
	move(right ? attr.x + attr.width - 32 : attr.x + 32, attr.y + 25);
	for (i = 0; i < 15; i++)
		click(Button5);
	check(drawer() == popup, "overflow scrolls without dismissing the drawer");
	/* The last tile belongs to Green two on workspace 1 (zero based 0). */
	move(right ? attr.x + 18 + 32 : attr.x + attr.width - 18 - 32, attr.y + 25);
	click(Button1);
	check(property(root, "_NET_ACTIVE_WINDOW") == second, "scrolling exposes and activates the last window");
	for (i = 0; i < 10; i++)
		XDestroyWindow(display, extra[i]);
	XDestroyWindow(display, second);
	pause_ms(300);
	move(320, 450);
	pause_ms(400);
	prepare_anchor();
	move(anchor_x, 96);
	click(Button1);
	check(drawer() == None, "a single window retains the normal icon click action");
	move(320, 450);
	popup = open_drawer();
	XDestroyWindow(display, other);
	pause_ms(700);
	check(drawer() == None, "closing the final window dismisses the empty drawer");
	move(320, 450);
	pause_ms(400);
	prepare_anchor();
	move(anchor_x, 96);
	pause_ms(950);
	check(drawer() == None, "an app with no remaining windows has no drawer");
	XCloseDisplay(display);
	return 0;
}
