/* Temporary window drawers for dock application icons. */
#ifndef WMDOCKPREVIEW_H_
#define WMDOCKPREVIEW_H_

#include "appicon.h"

void wDockPreviewEnter(WAppIcon *icon);
Bool wDockPreviewHasMultiple(WAppIcon *icon);
void wDockPreviewClick(WAppIcon *icon);
void wDockPreviewLeave(WAppIcon *icon);
void wDockPreviewHide(WScreen *scr);
void wDockPreviewHideForIcon(WAppIcon *icon);
Bool wDockPreviewHandleMotion(WScreen *scr, XMotionEvent *event);
Bool wDockPreviewSwitchingWorkspace(WScreen *scr);
Bool wDockPreviewHandleKey(WScreen *scr, XKeyEvent *event);
void wDockPreviewCapture(WWindow *wwin);
Bool wDockPreviewKeepsDockOpen(struct WDock *dock);
Bool wDockPreviewIsWindow(WScreen *scr, Window window);

#endif
