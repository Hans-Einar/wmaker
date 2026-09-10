# Window Maker: personal enhancements

This fork adds live desktop-file launchers and Ctrl+modifier left-drag resizing
on top of Window Maker. The original project documentation is in [README](README)
and [INSTALL](INSTALL).

## wmappicon

Create a native application icon in the current Clip from a `.desktop` file:

```sh
wmappicon /usr/share/applications/google-chrome.desktop
wmappicon /usr/share/applications/chatgpt.desktop
wmappicon -s
```

The command exits after sending its request. It does not start the application
or keep a dockapp process running. New icons are omnipresent and appear immediately.
Use `wmappicon --help` (or `-h`) for help.

`wmappicon -s` / `--save-icon-state` saves the current Dock, Clip, drawer and
workspace icon layout to `WMState`, preserving the existing application-session
entries. It does not capture currently open terminal windows. Previously saved
application-session entries are not removed; `SaveSessionOnExit = NO` prevents
normal exits from capturing a new application session.

### Window Maker changes

* `_WINDOWMAKER_COMMAND` accepts `DockLauncher` and `SaveDockState`. The launcher
  request carries its name, instance, class, command and icon path in the
  `_WINDOWMAKER_DOCK_LAUNCHER` root-window property.
* Native Clip icons are painted immediately and registered through Window Maker's
  omnipresent-icon machinery, so they follow workspace changes correctly.
* A single class hint matches either WM_CLASS component without case sensitivity;
  a complete instance/class pair uses exact matching. Empty components are
  preserved when icon identities are saved and restored.
* Matching windows without an application leader use Window Maker's existing
  application-icon emulation, respecting explicit window preferences. This makes
  the icon track the running application and use normal activation and workspace
  switching instead of launching another instance.
* Gray shading indicates startup only. Once attached, the icon has a window owner
  and normal appearance; closing the application restores the idle `...` marker.
* Icon-only saving passes valid state to the workspace serializer and respects
  disabled Dock/Clip/drawer settings. The earlier NULL-state crash is fixed.

The application binding uses Window Maker's existing activation and urgency
mechanisms; this fork does not add a D-Bus notification service.

### Matching and current limitations

`StartupWMClass` supplies the preferred hint. `X-WindowClass` is an optional local
extension for an explicit class. Without either key the desktop-file basename
is used as a heuristic. ChatGPT currently has a compatibility fallback to its
`Chatgpt` class because its desktop file omits `StartupWMClass`.

Class inference is not guaranteed for every application. The shell frontend
supports ordinary `Exec` entries and common icon locations; it is not a complete
Desktop Entry implementation (for example, `Terminal=true` and D-Bus activation
are not implemented). Create the icon before starting the application for reliable
binding. The IPC is asynchronous, uses a shared root property and currently has
no success acknowledgement; concurrent creation requests are not supported.

### Build and install

Use the normal Window Maker build described in [INSTALL](INSTALL). For a Git
checkout, regenerate the build files with `autoreconf -fi` before configuring.

```sh
./configure --prefix=/usr/local
make -j2
sudo make install
```

The build installs `wmappicon` and `wmappicon-ipc` together in `bin`. The helper
requires Xlib. The frontend requires a POSIX shell and standard text/file tools.
Restart Window Maker from its menu to load the patched window manager. Both the
patched manager and the helper are required; upstream Window Maker does not
implement these commands.

### Validation

Built without compiler warnings. Tested with actual Chrome and ChatGPT in an
isolated Xvfb session using separate application profiles: launch from the Clip,
startup shading cleared with a valid application owner, activation from another
workspace, closing the application, omnipresent movement, state saving and restored
icon identities. The user also confirmed the corrected behavior in the desktop
session.

## Ctrl+modifier left-drag resizing

Ctrl plus Window Maker's configured modifier (typically Alt) and left-drag
resizes a window. The existing modifier+middle-drag resize gesture remains
available.

## Reserve space for Clip icons

Enable the optional `NoWindowOverClip` preference (default: `NO`):

```sh
wdwrite WindowMaker NoWindowOverClip YES
```

After installing this build, restart Window Maker from its menu once. Subsequent
preference changes are read live. Maximize a window again to use the new area.

The window placement/maximization area excludes the bounding box of the current
workspace's visible Clip icons, with a four-pixel gap. The largest remaining
rectangle on one side is chosen: a column along the left edge reserves a left
strip, and a row along the top reserves a top strip. The calculation uses live
icon positions, includes omnipresent icons, and ignores application icons hidden
by a collapsed Clip (the Clip tile itself still reserves space). Only icons
intersecting the requested monitor contribute to its reserved area.

Existing Dock, icon-yard and workspace-border reservations remain in effect.
Already maximized windows are not automatically resized when icons move. Explicit
`FullMaximize` window attributes and fullscreen windows retain their existing
behavior. If icons span the whole monitor and leave no nonempty rectangle, the
original usable area is retained. Arbitrary scattered layouts can reserve more
space than a compact row or column.

Validation: `python3 tests/clip-maximize.py` starts isolated Xvfb servers and tests
actual window maximization with this option on/off, all four edges, a collapsed
Clip, workspace switching, omnipresent icons, `FullMaximize`, and disabled Clip.
Requires a built source tree, Python 3, Xvfb, a C compiler and Xlib development
files. It does not access the running desktop or personal configuration.
