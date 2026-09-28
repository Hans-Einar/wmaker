/*
 * Temporary window drawers for dock application icons.
 * Copyright (c) 2026 Window Maker contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "wconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#ifdef HAVE_XCOMPOSITE
#include <X11/extensions/Xcomposite.h>
#endif

#include "WindowMaker.h"
#include "dockpreview.h"
#include "dock.h"
#include "framewin.h"
#include "actions.h"
#include "client.h"
#include "stacking.h"
#include "workspace.h"
#include "misc.h"
#include "balloon.h"
#include "xinerama.h"

#define ICON_SIZE wPreferences.icon_size
#define HOVER_DELAY 700
#define POINTER_INTERVAL 100
#define LEAVE_TICKS 3
#define SEPARATOR_WIDTH 24
#define SCROLL_WIDTH 18
#define CLOSE_SIZE 16
#define FRAME_INTERVAL 16
#define SLIDE_DURATION 220
#define BURST_DURATION 220
#define BURST_PIECES 256

typedef struct {
	Window client;
	Pixmap tile, gray_tile;
	int offset, from_offset, width, workspace;
} PreviewEntry;

typedef struct {
	Pixmap tile;
	int x;
	signed char vx[BURST_PIECES], vy[BURST_PIECES];
} PreviewBurst;

typedef struct WDockPreview {
	WScreen *scr;
	WAppIcon *anchor;
	WCoreWindow *core;
	Pixmap buffer;
	Window hovered_client, revealed_client, original_focus, button_client;
	uint64_t layout_started;
	int burst_duration, old_scroll, target_scroll, max_width;
	PreviewBurst *bursts;
	int burst_count;
	uint64_t hover_started;
	Bool switching, cancel_pending, browsing, click_opened, pointer_grabbed, escape_grabbed;
	int display_workspace, original_last_workspace;
	KeyCode escape_key;
	Bool peek_raised;
	WMHandlerID timer;
	PreviewEntry *entries;
	int count, length, scroll, viewport;
	int x, y, anchor_x, anchor_y, workspace, outside;
	Bool right;
} WDockPreview;

static void paint(WObjDescriptor *desc, XEvent *event);
static void closePreview(WScreen *scr, Bool commit);
static void updateButton(WDockPreview *preview, int x, int y);

static uint64_t monotonicMilliseconds(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (uint64_t) now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

Bool wDockPreviewSwitchingWorkspace(WScreen *scr)
{
	return scr->dock_preview && scr->dock_preview->switching;
}

static Bool matches(WAppIcon *icon, WWindow *wwin)
{
	if (!wwin->frame || wwin->flags.destroyed || wwin->flags.internal_window ||
	    wwin->flags.is_dockapp || WFLAGP(wwin, skip_window_list))
		return False;
	if (icon->main_window != None && icon->main_window == wwin->main_window)
		return True;
	/* Include separately launched instances, not just the dock's group leader. */
	if (icon->wm_class && wwin->wm_class)
		return strcmp(icon->wm_class, wwin->wm_class) == 0;
	return icon->wm_instance && wwin->wm_instance &&
		strcmp(icon->wm_instance, wwin->wm_instance) == 0;
}

static Bool dockHasWindow(WDock *dock, WWindow *wwin)
{
	int i;

	if (!dock)
		return False;
	for (i = 1; i < dock->max_icons; i++) {
		if (dock->icon_array[i] && matches(dock->icon_array[i], wwin))
			return True;
	}
	return False;
}

static Bool hasDockIcon(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	WDrawerChain *drawer;
	int i;

	if (!wPreferences.dock_window_drawer)
		return False;
	if (dockHasWindow(scr->dock, wwin))
		return True;
	for (i = 0; i < scr->workspace_count; i++) {
		if (dockHasWindow(scr->workspaces[i]->clip, wwin))
			return True;
	}
	for (drawer = scr->drawers; drawer; drawer = drawer->next) {
		if (dockHasWindow(drawer->adrawer, wwin))
			return True;
	}
	return False;
}

/* Capturing is best effort: clients can resize/disappear between X requests. */
static int captureError;
static int captureErrorHandler(Display *display, XErrorEvent *event)
{
	(void) display;
	(void) event;
	captureError = 1;
	return 0;
}

void wDockPreviewCapture(WWindow *wwin)
{
	WScreen *scr = wwin->screen_ptr;
	XWindowAttributes attr;
	XImage *image = NULL;
	RImage *full, *small;
	int (*oldHandler)(Display *, XErrorEvent *);
	int width, height, size = ICON_SIZE - 6;
#ifdef HAVE_XCOMPOSITE
	Pixmap pixmap = None;
	int event, error, major, minor;
#endif

	if (!hasDockIcon(wwin) || !wwin->flags.mapped || wwin->flags.shaded ||
	    (!IS_OMNIPRESENT(wwin) && wwin->frame->workspace != scr->current_workspace))
		return;
	XSync(dpy, False);
	captureError = 0;
	oldHandler = XSetErrorHandler(captureErrorHandler);
#ifdef HAVE_XCOMPOSITE
	/* The compositor redirects WM frames. Never take over its redirection. */
	if (XCompositeQueryExtension(dpy, &event, &error) &&
	    XCompositeQueryVersion(dpy, &major, &minor) && (major > 0 || minor >= 2) &&
	    XGetWindowAttributes(dpy, wwin->frame->core->window, &attr) && attr.map_state == IsViewable) {
		pixmap = XCompositeNameWindowPixmap(dpy, wwin->frame->core->window);
		XSync(dpy, False);
		if (!captureError) {
			image = XGetImage(dpy, pixmap, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
			XFreePixmap(dpy, pixmap);
		}
	}
#endif
	/* Without a compositor, obscured window pixels are undefined. Keep the
	 * last good snapshot (or use an application icon) in that case. */
	if (!image && wwin->flags.preview_unobscured &&
	    XGetWindowAttributes(dpy, wwin->client_win, &attr) && attr.map_state == IsViewable)
		image = XGetImage(dpy, wwin->client_win, 0, 0, attr.width, attr.height, AllPlanes, ZPixmap);
	XSync(dpy, False);
	XSetErrorHandler(oldHandler);
	if (!image)
		return;
	image->red_mask = attr.visual->red_mask;
	image->green_mask = attr.visual->green_mask;
	image->blue_mask = attr.visual->blue_mask;
	full = RCreateImageFromXImage(scr->rcontext, image, NULL);
	XDestroyImage(image);
	if (!full)
		return;
	width = size;
	height = WMAX(1, size - WMFontHeight(scr->icon_title_font));
	if ((long) full->width * height > (long) full->height * width)
		height = WMAX(1, full->height * width / full->width);
	else
		width = WMAX(1, full->width * height / full->height);
	small = RSmoothScaleImage(full, width, height);
	RReleaseImage(full);
	if (small) {
		if (wwin->dock_preview)
			RReleaseImage(wwin->dock_preview);
		wwin->dock_preview = small;
	}
}

static void restorePeek(WDockPreview *preview)
{
	WWindow *wwin = preview->revealed_client ? wWindowFor(preview->revealed_client) : NULL;

	/* A temporary reveal leaves the iconic state and miniwindow intact. */
	if (wwin && !wwin->flags.destroyed && wwin->flags.miniaturized)
		wWindowUnmap(wwin);
	preview->revealed_client = None;
	/* Peeking changes only the server's stack, leaving the WM's normal
	 * ordering and keyboard focus intact. Reapply that order on hover-out. */
	if (preview->peek_raised)
		CommitStacking(preview->scr);
	preview->peek_raised = False;
	preview->hovered_client = None;
}

static Bool updatePeek(WDockPreview *preview)
{
	Window root, child, client = None, *windows;
	WWindow *wwin;
	WCoreWindow *top, *frame, *owner;
	WMBagIterator iter;
	unsigned int mask;
	int x, y, wx, wy, i, count, pass;
	Bool family;

	if (XQueryPointer(dpy, preview->scr->root_win, &root, &child, &x, &y, &wx, &wy, &mask) &&
	    child == preview->core->window) {
		updateButton(preview, x - preview->x, y - preview->y);
		x -= preview->x;
		if (preview->right)
			x = preview->core->width - 1 - x;
		if (x >= 0 && x < preview->viewport) {
			x += preview->scroll;
			for (i = 0; i < preview->count; i++) {
				PreviewEntry *entry = &preview->entries[i];
				if (x >= entry->offset && x < entry->offset + entry->width) {
					client = entry->client;
					break;
				}
			}
		}
	}
	if (!client)
		updateButton(preview, -1, -1);
	if (client != preview->hovered_client) {
		restorePeek(preview);
		preview->hovered_client = client;
		preview->hover_started = monotonicMilliseconds();
	}
	wwin = client ? wWindowFor(client) : NULL;
	if (!wwin || !wwin->frame || wwin->flags.destroyed || wwin->flags.hidden)
		return True;
	if (!IS_OMNIPRESENT(wwin) && wwin->frame->workspace != preview->scr->current_workspace) {
		if (monotonicMilliseconds() - preview->hover_started < HOVER_DELAY)
			return True;
		preview->switching = True;
		wWorkspaceChange(preview->scr, wwin->frame->workspace);
		preview->switching = False;
		if (preview->cancel_pending || preview->anchor->destroyed) {
			wDockPreviewHide(preview->scr);
			return False;
		}
		preview->display_workspace = preview->scr->current_workspace;
		preview->browsing = True;
		paint(&preview->core->descriptor, NULL);
		/* Workspace changes dispatch events; the target may have closed. */
		wwin = wWindowFor(client);
		if (!wwin || wwin->flags.destroyed) {
			wDockPreviewHide(preview->scr);
			return False;
		}
	}
	if (preview->peek_raised)
		return True;
	if (wwin->flags.miniaturized) {
		preview->revealed_client = client;
		wWindowMap(wwin);
	}
	if (!wwin->flags.mapped && !wwin->flags.shaded)
		return True;

	/* Raise the family within each stacking level, keeping transients above
	 * their owner and docks/menus above normal windows. The normal lists
	 * stay untouched, and no frame pointers outlive this function. */
	windows = wmalloc(sizeof(Window) * preview->scr->window_count);
	count = 0;
	WM_ETARETI_BAG(preview->scr->stacking_list, top, iter) {
		for (pass = 0; pass < 2; pass++) {
			for (frame = top; frame; frame = frame->stacking->under) {
				family = False;
				for (owner = frame; owner; owner = owner->stacking->child_of) {
					if (owner == wwin->frame->core) {
						family = True;
						break;
					}
				}
				if (family == (pass == 0))
					windows[count++] = frame->window;
			}
		}
	}
	XRestackWindows(dpy, windows, count);
	wfree(windows);
	preview->peek_raised = True;
	return True;
}

static void closePreview(WScreen *scr, Bool commit)
{
	WDockPreview *preview = scr->dock_preview;
	WWindow *focus;
	int i;

	if (!preview)
		return;
	/* wWorkspaceChange processes events. Defer cancellation until its
	 * caller regains control, so neither timer nor callback uses freed data. */
	if (preview->switching) {
		preview->cancel_pending = True;
		return;
	}
	preview->switching = True;
	if (preview->timer) {
		WMDeleteTimerHandler(preview->timer);
		preview->timer = NULL;
	}
	restorePeek(preview);
	if (!commit && preview->core && WCHECK_STATE(WSTATE_NORMAL)) {
		if (preview->workspace != scr->current_workspace && preview->workspace < scr->workspace_count)
			wWorkspaceChange(scr, preview->workspace);
		if (preview->browsing) {
			focus = wWindowFor(preview->original_focus);
			if (focus && !focus->flags.destroyed && !focus->flags.miniaturized &&
			    !focus->flags.hidden && (focus->flags.mapped || focus->flags.shaded))
				wSetFocusTo(scr, focus);
			scr->last_workspace = preview->original_last_workspace;
		}
	}
	scr->dock_preview = NULL;
	if (preview->pointer_grabbed)
		XUngrabPointer(dpy, CurrentTime);
	if (preview->escape_grabbed)
		XUngrabKey(dpy, preview->escape_key, AnyModifier, scr->root_win);
	if (preview->core) {
		RemoveFromStackList(preview->core);
		wCoreDestroy(preview->core);
	}
	for (i = 0; i < preview->count; i++) {
		if (preview->entries[i].tile)
			XFreePixmap(dpy, preview->entries[i].tile);
		if (preview->entries[i].gray_tile)
			XFreePixmap(dpy, preview->entries[i].gray_tile);
	}
	for (i = 0; i < preview->burst_count; i++)
		XFreePixmap(dpy, preview->bursts[i].tile);
	wfree(preview->bursts);
	if (preview->buffer)
		XFreePixmap(dpy, preview->buffer);
	wrelease(preview->anchor);
	wfree(preview->entries);
	wfree(preview);
}

void wDockPreviewHide(WScreen *scr)
{
	closePreview(scr, False);
}

void wDockPreviewHideForIcon(WAppIcon *icon)
{
	WScreen *scr = icon->icon->core->screen_ptr;
	if (scr->dock_preview && scr->dock_preview->anchor == icon)
		wDockPreviewHide(scr);
}

Bool wDockPreviewHandleKey(WScreen *scr, XKeyEvent *event)
{
	WDockPreview *preview = scr->dock_preview;

	if (!preview || !preview->core)
		return False;
	if (event->keycode == preview->escape_key || preview->switching) {
		wDockPreviewHide(scr);
		return True;
	}
	/* A window-manager shortcut ends this interaction before it can grab
	 * input for a menu, move, or another modal operation. */
	wDockPreviewHide(scr);
	return False;
}

Bool wDockPreviewIsWindow(WScreen *scr, Window window)
{
	return scr->dock_preview && scr->dock_preview->core &&
		scr->dock_preview->core->window == window;
}

Bool wDockPreviewKeepsDockOpen(WDock *dock)
{
	WDockPreview *preview = dock->screen_ptr->dock_preview;
	WDock *anchor;

	if (!preview || !preview->core || preview->anchor->destroyed)
		return False;
	anchor = preview->anchor->dock;
	return anchor == dock || (anchor && anchor->type == WM_DRAWER && dock == dock->screen_ptr->dock);
}

static Bool pointerInside(WDockPreview *preview, Bool anchorOnly)
{
	Window root, child;
	int x, y, wx, wy;
	unsigned int mask;

	if (!XQueryPointer(dpy, preview->scr->root_win, &root, &child, &x, &y, &wx, &wy, &mask) ||
	    (mask & (Button1Mask | Button2Mask | Button3Mask)))
		return False;
	if (x >= preview->anchor_x && x < preview->anchor_x + ICON_SIZE &&
	    y >= preview->anchor_y && y < preview->anchor_y + ICON_SIZE)
		return True;
	return !anchorOnly && preview->core && x >= preview->x &&
		x < preview->x + preview->core->width && y >= preview->y && y < preview->y + ICON_SIZE;
}

static void drawText(WDockPreview *preview, Drawable drawable, const char *text, int x, int y, int width)
{
	WScreen *scr = preview->scr;
	char *shortened = ShrinkString(scr->icon_title_font, text, width);

	WMDrawString(scr->wmscreen, drawable, scr->white, scr->icon_title_font,
		     x, y, shortened, strlen(shortened));
	wfree(shortened);
}

static Pixmap makeTile(WDockPreview *preview, WWindow *wwin)
{
	WScreen *scr = preview->scr;
	Pixmap tile, picture;
	RImage *image;
	int labelHeight = WMFontHeight(scr->icon_title_font) + 2;
	int width, height;

	wDockPreviewCapture(wwin);
	image = wwin->dock_preview;
	if (!image)
		image = wwin->net_icon_image ? wwin->net_icon_image : preview->anchor->icon->file_image;
	tile = XCreatePixmap(dpy, scr->root_win, ICON_SIZE, ICON_SIZE, scr->w_depth);
	XSetForeground(dpy, scr->draw_gc, scr->dark_pixel);
	XFillRectangle(dpy, tile, scr->draw_gc, 0, 0, ICON_SIZE, ICON_SIZE);
	if (image) {
		width = ICON_SIZE - 6;
		height = WMAX(1, ICON_SIZE - labelHeight - 4);
		if ((long) image->width * height > (long) image->height * width)
			height = WMAX(1, image->height * width / image->width);
		else
			width = WMAX(1, image->width * height / image->height);
		image = RSmoothScaleImage(image, width, height);
		if (image) {
			if (RConvertImage(scr->rcontext, image, &picture)) {
				XCopyArea(dpy, picture, tile, scr->draw_gc, 0, 0, width, height,
					  (ICON_SIZE - width) / 2, (ICON_SIZE - labelHeight - height) / 2);
				XFreePixmap(dpy, picture);
			}
			RReleaseImage(image);
		}
	}
	drawText(preview, tile, wwin->frame->title ? wwin->frame->title : _("Untitled"),
		 3, ICON_SIZE - labelHeight, ICON_SIZE - 6);
	XSetForeground(dpy, scr->draw_gc, scr->light_pixel);
	XDrawRectangle(dpy, tile, scr->draw_gc, 0, 0, ICON_SIZE - 1, ICON_SIZE - 1);
	return tile;
}

static Pixmap grayscaleTile(WScreen *scr, Pixmap tile)
{
	RImage *image = RCreateImageFromDrawable(scr->rcontext, tile, None);
	Pixmap gray = None;
	unsigned char *pixel;
	int i, channels;

	if (!image)
		return None;
	channels = image->format == RRGBAFormat ? 4 : 3;
	for (i = 0, pixel = image->data; i < image->width * image->height; i++, pixel += channels) {
		unsigned char level = (30 * pixel[0] + 59 * pixel[1] + 11 * pixel[2]) / 100;
		pixel[0] = pixel[1] = pixel[2] = level;
	}
	RConvertImage(scr->rcontext, image, &gray);
	RReleaseImage(image);
	return gray;
}

/* Positions are logical distances from the launcher; mirroring happens only
 * when painting/hit-testing, so the close button is always visually top-right. */
static int slideOffset(WDockPreview *preview, PreviewEntry *entry)
{
	int elapsed;
	if (!preview->layout_started)
		return entry->offset;
	elapsed = (int)(monotonicMilliseconds() - preview->layout_started) - preview->burst_duration;
	elapsed = WMAX(0, WMIN(SLIDE_DURATION, elapsed));
	return entry->offset + (entry->from_offset - entry->offset) *
		(SLIDE_DURATION - elapsed) * (SLIDE_DURATION - elapsed) / (SLIDE_DURATION * SLIDE_DURATION);
}

static int entryX(WDockPreview *preview, PreviewEntry *entry)
{
	int x = slideOffset(preview, entry) - preview->scroll;
	return preview->right ? preview->core->width - x - entry->width : x;
}

static PreviewEntry *entryAt(WDockPreview *preview, int x, int y)
{
	int i, logical = preview->right ? preview->core->width - 1 - x : x;
	if (preview->layout_started || y < 0 || y >= ICON_SIZE || logical < 0 || logical >= preview->viewport)
		return NULL;
	logical += preview->scroll;
	for (i = 0; i < preview->count; i++) {
		PreviewEntry *entry = &preview->entries[i];
		if (entry->client && logical >= entry->offset && logical < entry->offset + entry->width)
			return entry;
	}
	return NULL;
}

static Bool canClose(WWindow *wwin)
{
	return wwin && !wwin->flags.destroyed && wwin->protocols.DELETE_WINDOW && !WFLAGP(wwin, no_closable);
}

static void updateButton(WDockPreview *preview, int x, int y)
{
	PreviewEntry *entry = entryAt(preview, x, y);
	Window client = entry ? entry->client : None;
	if (preview->button_client != client) {
		preview->button_client = client;
		paint(&preview->core->descriptor, NULL);
	}
}

Bool wDockPreviewHandleMotion(WScreen *scr, XMotionEvent *event)
{
	WDockPreview *preview = scr ? scr->dock_preview : NULL;
	if (!preview || !preview->core || event->window != preview->core->window)
		return False;
	if (!preview->switching)
		updateButton(preview, event->x, event->y);
	return True;
}

/* The dock's Kaboom uses small fragments with upward velocity and gravity.
 * Draw that effect into our backing pixmap on timer ticks: DoKaboom itself
 * unmaps its window and blocks the event loop while drawing on the root. */
static void paintBursts(WDockPreview *preview)
{
	int i, row, col, k, size = ICON_KABOOM_PIECE_SIZE;
	int elapsed = (int)(monotonicMilliseconds() - preview->layout_started);
	if (!preview->burst_count || elapsed >= preview->burst_duration)
		return;
	for (i = 0; i < preview->burst_count; i++) {
		PreviewBurst *burst = &preview->bursts[i];
		for (row = 0, k = 0; row < ICON_SIZE / size; row++) {
			for (col = 0; col < ICON_SIZE / size && k < BURST_PIECES; col++, k++) {
				int x, y, frame = elapsed / 20;
				if (!burst->vy[k])
					continue;
				x = burst->x + col * size + burst->vx[k] * frame / 2;
				y = row * size + burst->vy[k] * frame / 2 + frame * frame / 8;
				XCopyArea(dpy, burst->tile, preview->buffer, preview->scr->draw_gc,
					  col * size, row * size, size, size, x, y);
			}
		}
	}
}

static void paint(WObjDescriptor *desc, XEvent *event)
{
	WDockPreview *preview = desc->parent;
	WScreen *scr = preview->scr;
	int i, x, control;
	char label[16];

	(void) event;
	XSetForeground(dpy, scr->draw_gc, scr->dark_pixel);
	XFillRectangle(dpy, preview->buffer, scr->draw_gc, 0, 0, preview->core->width, ICON_SIZE);
	for (i = 0; i < preview->count; i++) {
		PreviewEntry *entry = &preview->entries[i];
		x = entryX(preview, entry);
		if (entry->tile) {
			WWindow *wwin = wWindowFor(entry->client);
			Bool current = wwin && wwin->frame && (IS_OMNIPRESENT(wwin) ||
					       wwin->frame->workspace == scr->current_workspace);
			Pixmap tile = !current && entry->gray_tile ? entry->gray_tile : entry->tile;

			XCopyArea(dpy, tile, preview->buffer, scr->draw_gc,
				  0, 0, ICON_SIZE, ICON_SIZE, x, 0);
			if (entry->client == preview->button_client && !preview->layout_started) {
				int bx = x + ICON_SIZE - CLOSE_SIZE - 2;
				XSetForeground(dpy, scr->draw_gc, scr->black_pixel);
				XFillRectangle(dpy, preview->buffer, scr->draw_gc, bx, 2, CLOSE_SIZE, CLOSE_SIZE);
				XSetForeground(dpy, scr->draw_gc, canClose(wwin) ? scr->white_pixel : scr->light_pixel);
				XDrawRectangle(dpy, preview->buffer, scr->draw_gc, bx, 2, CLOSE_SIZE - 1, CLOSE_SIZE - 1);
				XDrawLine(dpy, preview->buffer, scr->draw_gc, bx + 4, 6, bx + CLOSE_SIZE - 5, CLOSE_SIZE - 3);
				XDrawLine(dpy, preview->buffer, scr->draw_gc, bx + CLOSE_SIZE - 5, 6, bx + 4, CLOSE_SIZE - 3);
			}
		} else {
			XSetForeground(dpy, scr->draw_gc, scr->light_pixel);
			XDrawLine(dpy, preview->buffer, scr->draw_gc, x + 2, 4, x + 2, ICON_SIZE - 5);
			snprintf(label, sizeof(label), "%d", entry->workspace + 1);
			drawText(preview, preview->buffer, label, x + 5,
				 (ICON_SIZE - WMFontHeight(scr->icon_title_font)) / 2, SEPARATOR_WIDTH - 6);
		}
	}
	paintBursts(preview);
	if (preview->length > preview->viewport) {
		control = preview->right ? 0 : preview->viewport;
		XSetForeground(dpy, scr->draw_gc, scr->black_pixel);
		XFillRectangle(dpy, preview->buffer, scr->draw_gc, control, 0, SCROLL_WIDTH, ICON_SIZE);
		drawText(preview, preview->buffer, "<", control + 3, 6, SCROLL_WIDTH - 4);
		drawText(preview, preview->buffer, ">", control + 3, ICON_SIZE / 2 + 6, SCROLL_WIDTH - 4);
	}
	XCopyArea(dpy, preview->buffer, preview->core->window, scr->draw_gc,
		  0, 0, preview->core->width, ICON_SIZE, 0, 0);
}

static void mouseDown(WObjDescriptor *desc, XEvent *event)
{
	WDockPreview *preview = desc->parent;
	int i, x = event->xbutton.x, direction = 0;
	Bool control = preview->length > preview->viewport &&
		(preview->right ? x < SCROLL_WIDTH : x >= preview->viewport);

	if (preview->switching) {
		preview->cancel_pending = True;
		return;
	}
	if (x < 0 || x >= preview->core->width || event->xbutton.y < 0 || event->xbutton.y >= ICON_SIZE) {
		if (!preview->browsing && event->xbutton.button == Button1 &&
		    !(event->xbutton.state & (ShiftMask | ControlMask | wPreferences.modifier_mask)) &&
		    event->xbutton.x_root >= preview->anchor_x && event->xbutton.x_root < preview->anchor_x + ICON_SIZE &&
		    event->xbutton.y_root >= preview->anchor_y && event->xbutton.y_root < preview->anchor_y + ICON_SIZE &&
		    !preview->anchor->destroyed && wDockPreviewHasMultiple(preview->anchor)) {
			preview->click_opened = True;
			return;
		}
		wDockPreviewHide(preview->scr);
		return;
	}
	/* Do not let queued clicks close a neighbour while tiles fill the gap. */
	if (preview->layout_started)
		return;
	if (event->xbutton.button == Button1) {
		PreviewEntry *entry = entryAt(preview, x, event->xbutton.y);
		if (entry && x >= entryX(preview, entry) + ICON_SIZE - CLOSE_SIZE - 2 &&
		    x < entryX(preview, entry) + ICON_SIZE - 2 &&
		    event->xbutton.y >= 2 && event->xbutton.y < 2 + CLOSE_SIZE) {
			WWindow *wwin = wWindowFor(entry->client);
			preview->click_opened = True;
			if (canClose(wwin))
				wClientSendProtocol(wwin, w_global.atom.wm.delete_window, event->xbutton.time);
			else
				XBell(dpy, 0);
			/* Only remove a tile after the application actually closes it. */
			return;
		}
	}
	if (event->xbutton.button == Button4)
		direction = -1;
	else if (event->xbutton.button == Button5)
		direction = 1;
	else if (event->xbutton.button == Button1 && control)
		direction = event->xbutton.y < ICON_SIZE / 2 ? -1 : 1;
	if (direction) {
		restorePeek(preview);
		preview->scroll = WMAX(0, WMIN(preview->length - preview->viewport,
			preview->scroll + direction * ICON_SIZE));
		paint(desc, NULL);
		return;
	}
	if (event->xbutton.button == Button1) {
		if (preview->right)
			x = preview->core->width - 1 - x;
		x += preview->scroll;
		for (i = 0; i < preview->count; i++) {
			PreviewEntry *entry = &preview->entries[i];
			if (entry->client && x >= entry->offset && x < entry->offset + entry->width) {
				Window client = entry->client;
				WWindow *wwin;

				closePreview(preview->scr, True);
				wwin = wWindowFor(client);
				if (wwin && !wwin->flags.destroyed)
					wWindowSingleFocus(wwin);
				return;
			}
		}
	} else {
		wDockPreviewHide(preview->scr);
	}
}

static void freeBursts(WDockPreview *preview)
{
	int i;
	for (i = 0; i < preview->burst_count; i++)
		XFreePixmap(dpy, preview->bursts[i].tile);
	wfree(preview->bursts);
	preview->bursts = NULL;
	preview->burst_count = 0;
}

static Bool finishLayout(WDockPreview *preview)
{
	int width;
	freeBursts(preview);
	preview->layout_started = 0;
	if (!preview->count) {
		wDockPreviewHide(preview->scr);
		return False;
	}
	width = WMIN(preview->length, preview->max_width);
	preview->viewport = width - (preview->length > width ? SCROLL_WIDTH : 0);
	preview->scroll = WMIN(preview->scroll, WMAX(0, preview->length - preview->viewport));
	preview->x = preview->right ? preview->anchor_x - width : preview->anchor_x + ICON_SIZE;
	/* The original backing pixmap is at least as large as this shrinking window. */
	wCoreConfigure(preview->core, preview->x, preview->y, width, ICON_SIZE);
	paint(&preview->core->descriptor, NULL);
	return True;
}

static Bool removeDeadEntries(WDockPreview *preview)
{
	Bool *keep = wmalloc(sizeof(Bool) * preview->count), changed = False;
	int i, j, next, length = 0, count = 0, width, viewport;

	for (i = 0; i < preview->count; i++) {
		PreviewEntry *entry = &preview->entries[i];
		WWindow *wwin = entry->client ? wWindowFor(entry->client) : NULL;
		keep[i] = entry->client && wwin && matches(preview->anchor, wwin) &&
			(IS_OMNIPRESENT(wwin) || wwin->frame->workspace == entry->workspace);
		if (entry->client && !keep[i])
			changed = True;
	}
	if (!changed) {
		wfree(keep);
		return True;
	}
	/* A separator survives exactly as long as its original workspace group. */
	for (i = 0; i < preview->count; i++) {
		if (preview->entries[i].client)
			continue;
		for (next = i + 1; next < preview->count && preview->entries[next].client; next++)
			keep[i] |= keep[next];
	}
	restorePeek(preview);
	preview->button_client = None;
	for (i = 0; i < preview->count; i++) {
		PreviewEntry entry = preview->entries[i];
		if (keep[i]) {
			entry.from_offset = entry.offset;
			entry.offset = length;
			length += entry.width;
			preview->entries[count++] = entry;
		} else {
			if (entry.client && !wWindowFor(entry.client) &&
			    wPreferences.dock_window_drawer_explosion && !wPreferences.no_animations) {
				PreviewBurst *burst;
				preview->bursts = wrealloc(preview->bursts, sizeof(PreviewBurst) * (preview->burst_count + 1));
				burst = &preview->bursts[preview->burst_count++];
				burst->x = entryX(preview, &entry);
				if (entry.workspace != preview->scr->current_workspace && entry.gray_tile) {
					burst->tile = entry.gray_tile;
					entry.gray_tile = None;
				} else {
					burst->tile = entry.tile;
					entry.tile = None;
				}
				for (j = 0; j < BURST_PIECES; j++) {
					burst->vx[j] = rand() % 17 - 8;
					burst->vy[j] = rand() % 2 ? -15 - rand() % 7 : 0;
				}
			}
			if (entry.tile)
				XFreePixmap(dpy, entry.tile);
			if (entry.gray_tile)
				XFreePixmap(dpy, entry.gray_tile);
		}
	}
	wfree(keep);
	preview->count = count;
	preview->length = length;
	preview->old_scroll = preview->scroll;
	width = WMIN(length, preview->max_width);
	viewport = width - (length > width ? SCROLL_WIDTH : 0);
	preview->target_scroll = WMIN(preview->scroll, WMAX(0, length - viewport));
	preview->burst_duration = preview->burst_count ? BURST_DURATION : 0;
	if (wPreferences.no_animations)
		return finishLayout(preview);
	preview->layout_started = monotonicMilliseconds();
	paint(&preview->core->descriptor, NULL);
	return True;
}

static Bool advanceLayout(WDockPreview *preview)
{
	int elapsed = (int)(monotonicMilliseconds() - preview->layout_started) - preview->burst_duration;
	if (elapsed >= SLIDE_DURATION || wPreferences.no_animations)
		return finishLayout(preview);
	elapsed = WMAX(0, elapsed);
	preview->scroll = preview->target_scroll + (preview->old_scroll - preview->target_scroll) *
		(SLIDE_DURATION - elapsed) * (SLIDE_DURATION - elapsed) / (SLIDE_DURATION * SLIDE_DURATION);
	paint(&preview->core->descriptor, NULL);
	return True;
}

static void checkPointer(void *data)
{
	WDockPreview *preview = data;
	WAppIcon *icon = preview->anchor;

	preview->timer = NULL;
	if (!wPreferences.dock_window_drawer || icon->destroyed || !icon->docked || !icon->dock ||
	    preview->core->height != ICON_SIZE ||
	    (!preview->browsing && (icon->x_pos != preview->anchor_x || icon->y_pos != preview->anchor_y)) ||
	    preview->display_workspace != preview->scr->current_workspace) {
		wDockPreviewHide(preview->scr);
		return;
	}
	if (preview->layout_started) {
		if (!advanceLayout(preview))
			return;
	} else if (!removeDeadEntries(preview)) {
		return;
	}
	if (!preview->layout_started && !updatePeek(preview))
		return;
	if (pointerInside(preview, False) || preview->browsing || preview->click_opened || preview->layout_started)
		preview->outside = 0;
	else if (++preview->outside >= LEAVE_TICKS) {
		wDockPreviewHide(preview->scr);
		return;
	}
	preview->timer = WMAddTimerHandler(preview->layout_started ? FRAME_INTERVAL : POINTER_INTERVAL, checkPointer, preview);
}

static void appendEntry(WDockPreview *preview, WWindow *wwin, int workspace)
{
	PreviewEntry *entry;

	preview->entries = wrealloc(preview->entries, sizeof(PreviewEntry) * (preview->count + 1));
	entry = &preview->entries[preview->count++];
	memset(entry, 0, sizeof(*entry));
	entry->workspace = workspace;
	entry->offset = preview->length;
	entry->width = wwin ? ICON_SIZE : SEPARATOR_WIDTH;
	if (wwin) {
		entry->client = wwin->client_win;
		entry->tile = makeTile(preview, wwin);
		entry->gray_tile = grayscaleTile(preview->scr, entry->tile);
	}
	preview->length += entry->width;
}

static void showPreview(void *data)
{
	WDockPreview *preview = data;
	WScreen *scr = preview->scr;
	WWindow *wwin;
	WMRect head;
	int pass, workspace, available, width;
	Bool first;

	preview->timer = NULL;
	if (!wPreferences.dock_window_drawer || !preview->anchor->docked ||
	    preview->anchor_x != preview->anchor->x_pos || preview->anchor_y != preview->anchor->y_pos ||
	    !pointerInside(preview, True)) {
		wDockPreviewHide(scr);
		return;
	}
	preview->workspace = preview->display_workspace = scr->current_workspace;
	preview->original_last_workspace = scr->last_workspace;
	preview->original_focus = scr->focused_window ? scr->focused_window->client_win : None;
	/* Current workspace first; all remaining groups in workspace order. */
	for (pass = -1; pass < scr->workspace_count; pass++) {
		if (pass == scr->current_workspace)
			continue;
		workspace = pass < 0 ? scr->current_workspace : pass;
		first = True;
		for (wwin = scr->focused_window; wwin; wwin = wwin->prev) {
			if (!matches(preview->anchor, wwin) ||
			    (IS_OMNIPRESENT(wwin) ? workspace != scr->current_workspace : wwin->frame->workspace != workspace))
				continue;
			if (first && (preview->count || workspace != scr->current_workspace))
				appendEntry(preview, NULL, workspace);
			first = False;
			appendEntry(preview, wwin, workspace);
		}
	}
	if (!preview->count) {
		wDockPreviewHide(scr);
		return;
	}
	head = wGetRectForHead(scr, wGetHeadForPoint(scr, wmkpoint(preview->anchor_x + ICON_SIZE / 2,
								preview->anchor_y + ICON_SIZE / 2)));
	/* Unlike the main Dock, Clips can be placed anywhere and their
	 * on_right_side flag does not describe the launcher position. */
	preview->right = preview->anchor_x - head.pos.x >
		(int)(head.pos.x + head.size.width) - preview->anchor_x - ICON_SIZE;
	available = preview->right ? preview->anchor_x - head.pos.x :
		head.pos.x + head.size.width - preview->anchor_x - ICON_SIZE;
	if (available < ICON_SIZE + SCROLL_WIDTH) {
		wDockPreviewHide(scr);
		return;
	}
	preview->max_width = available;
	width = WMIN(preview->length, available);
	preview->viewport = width - (preview->length > width ? SCROLL_WIDTH : 0);
	preview->x = preview->right ? preview->anchor_x - width : preview->anchor_x + ICON_SIZE;
	preview->y = WMAX(head.pos.y, WMIN(preview->anchor_y, (int)(head.pos.y + head.size.height) - ICON_SIZE));
	preview->core = wCoreCreateTopLevel(scr, preview->x, preview->y, width, ICON_SIZE, 0,
					   scr->w_depth, scr->w_visual, scr->w_colormap, scr->black_pixel);
	/* Draw text into a pixmap, not the transient window. WINGs keeps an Xft
	 * picture for its last text drawable; window destruction invalidates it. */
	preview->buffer = XCreatePixmap(dpy, scr->root_win, width, ICON_SIZE, scr->w_depth);
	preview->core->descriptor.parent = preview;
	preview->core->descriptor.handle_expose = paint;
	preview->core->descriptor.handle_mousedown = mouseDown;
	preview->core->stacking = wmalloc(sizeof(WStacking));
	preview->core->stacking->window_level = WMPopUpLevel;
	AddToStackList(preview->core);
	XSetWindowBackground(dpy, preview->core->window, scr->dark_pixel);
	XStoreName(dpy, preview->core->window, "Window Maker dock window drawer");
#ifdef BALLOON_TEXT
	wBalloonHide(scr);
#endif
	XMapRaised(dpy, preview->core->window);
	if (XGrabPointer(dpy, preview->core->window, False,
			 ButtonPressMask | ButtonReleaseMask | PointerMotionMask,
			 GrabModeAsync, GrabModeAsync, None, None, CurrentTime) != GrabSuccess) {
		wDockPreviewHide(scr);
		return;
	}
	preview->pointer_grabbed = True;
	preview->escape_key = XKeysymToKeycode(dpy, XK_Escape);
	XGrabKey(dpy, preview->escape_key, AnyModifier, scr->root_win, False, GrabModeAsync, GrabModeAsync);
	preview->escape_grabbed = True;
	preview->timer = WMAddTimerHandler(preview->layout_started ? FRAME_INTERVAL : POINTER_INTERVAL, checkPointer, preview);
}

void wDockPreviewEnter(WAppIcon *icon)
{
	WScreen *scr = icon->icon->core->screen_ptr;
	WDockPreview *preview = scr->dock_preview;

	if ((preview && preview->switching) || !wPreferences.dock_window_drawer || !icon->docked || !icon->dock ||
	    icon == icon->dock->icon_array[0])
		return;
	if (preview && preview->anchor == icon)
		return;
	wDockPreviewHide(scr);
	preview = wmalloc(sizeof(*preview));
	preview->scr = scr;
	preview->anchor = wretain(icon);
	preview->anchor_x = icon->x_pos;
	preview->anchor_y = icon->y_pos;
	scr->dock_preview = preview;
	preview->timer = WMAddTimerHandler(HOVER_DELAY, showPreview, preview);
}

void wDockPreviewLeave(WAppIcon *icon)
{
	WScreen *scr = icon->icon->core->screen_ptr;
	WDockPreview *preview = scr->dock_preview;

	/* Ignore crossings into an embedded dockapp child; the timer checks the
	 * actual pointer position. A mapped drawer gets a short crossing grace. */
	if (preview && !preview->switching && preview->anchor == icon && !preview->core && !pointerInside(preview, True))
		wDockPreviewHide(scr);
}

Bool wDockPreviewHasMultiple(WAppIcon *icon)
{
	WWindow *wwin;
	int count = 0;

	if (!wPreferences.dock_window_drawer || !icon->docked || !icon->dock ||
	    icon == icon->dock->icon_array[0])
		return False;
	for (wwin = icon->icon->core->screen_ptr->focused_window; wwin; wwin = wwin->prev) {
		if (matches(icon, wwin) && ++count > 1)
			return True;
	}
	return False;
}

void wDockPreviewClick(WAppIcon *icon)
{
	WScreen *scr = icon->icon->core->screen_ptr;
	WDockPreview *preview;

	if (!wDockPreviewHasMultiple(icon))
		return;
	wDockPreviewEnter(icon);
	preview = scr->dock_preview;
	if (!preview || preview->anchor != icon)
		return;
	preview->click_opened = True;
	if (!preview->core) {
		if (preview->timer)
			WMDeleteTimerHandler(preview->timer);
		preview->timer = NULL;
		showPreview(preview);
	}
}
