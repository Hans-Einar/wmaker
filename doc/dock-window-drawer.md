# Dock window drawer

Hover over a launcher in the Dock, Clip, or a persistent drawer for 700 milliseconds
to open its window drawer. A plain left-click opens it immediately when the
application has more than one window. Single-window icons retain their normal
click action, and dragging or modifier-clicks retain their existing behavior.
This is enabled by default (`DockWindowDrawer = YES` in
`~/GNUstep/Defaults/WindowMaker`). Disable it with:

```
wdwrite WindowMaker DockWindowDrawer NO
```

Each tile represents a managed application window, including separate instances
with the same `WM_CLASS`. Processes without windows and windows excluded from
the window list do not appear. The workspace where the drawer opens appears
nearest the launcher. Other workspaces follow in workspace order, separated by
a divider bearing the workspace number. Omnipresent windows appear once.
**This order remains fixed for the entire interaction**, including workspace
previews.

The drawer opens toward the side with more room on the launcher’s monitor.
Tiles use the configured icon size (normally 64 × 64) and a shortened window
title. Windows on the active workspace have color thumbnails; windows on other
workspaces have grayscale thumbnails.

Hover a tile to temporarily raise its window. A minimized window on the active
workspace is temporarily displayed while keeping its minimized state and icon.
Moving to another tile, a separator, the launcher, or outside the drawer
restores the normal stacking order and hides any temporary minimized preview.
Keyboard focus is unchanged by a same-workspace peek. Transient dialogs stay
above their owner, and higher-level docks and menus retain their priority.
Hidden applications still require a click to unhide.

Hold the pointer over an other-workspace tile for 700 milliseconds to temporarily
switch to that workspace. Its tiles regain color and the previous workspace’s
tiles become grayscale, **without moving any tiles**. The drawer stays open
until explicitly dismissed or a window is selected. Further tile hovers can
preview additional workspaces. Escape or a click outside the drawer cancels
and returns to the workspace and focus from which the drawer was opened.
Clicking a tile selects that window, restores it if needed, and commits its
workspace; it then stays raised.

An ordinary hover-opened drawer closes when the pointer moves away, with a
short grace period for crossing into it. A click-opened drawer stays open until
an outside click, Escape, or a selection. If the list exceeds the monitor width,
use the mouse wheel or the two arrow buttons at its far end to scroll.

## Snapshots

This first implementation uses snapshots, not live video. When libXcomposite
headers and the library are available at build time, it can read the running
compositor's redirected window image without changing compositor ownership.
It does not require OpenGL or a particular compositor.

Small snapshots are cached before windows leave the visible workspace, are
minimized, hidden, or shaded. Without a compositor, only unobscured windows can
be captured reliably. A window that has never had a usable snapshot falls back
to its application icon. Unmapped windows retain their last snapshot; they are
never temporarily shown just to capture them. Cached images are freed with the
window, and drawer pixmaps/timers are freed when the drawer closes.

The drawer is transient. It uses the Dock's icon sizing and drawer direction,
but is separate from persistent launcher drawers, so it cannot save preview
windows as launchers or alter the saved dock arrangement. A Clip or persistent
drawer stays expanded while its window previews are in use, then resumes its
normal auto-collapse and auto-lower behavior.
The larger vertical preview layout is not implemented yet.

## Build and verify

Build using the normal `./autogen.sh`, `./configure`, and `make` workflow.
`make check` includes the existing project checks and a regression test for
nested timer dispatch during workspace changes. The integration test uses
an isolated Xvfb display and a temporary Window Maker configuration:

```
test/dockpreview-xvfb.sh left composite
test/dockpreview-xvfb.sh right plain
test/dockpreview-xvfb.sh left composite clip
test/dockpreview-xvfb.sh right plain clip-auto
test/dockpreview-xvfb.sh right composite drawer
```

It requires Xvfb and libXtst development files; the `composite` run also
requires xcompmgr. It verifies the delay/cancellation, grouping and screenshots,
both dock directions, omnipresent Clip launchers, Clip auto-collapse, pointer
crossing, temporary hover raising and restoration, click-to-keep-on-top,
grayscale workspace browsing with fixed order, Escape/outside-click rollback,
minimized hover previews, click activation, closing clients, and overflow
scrolling, including repeated workspace hover with workspace-name effects.
Test logs and a
PPM screenshot are retained in the printed temporary directory. It does not
replace the running desktop window manager or install the new binary.
