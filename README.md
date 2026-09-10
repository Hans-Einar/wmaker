# Window Maker: personal enhancements

This fork adds live desktop-file launchers and application-icon improvements
on top of Window Maker. The original project documentation is in [README](README)
and [INSTALL](INSTALL).

The running list of implemented fork changes is in
[release notes: WIP / Unreleased](RELEASE_NOTES.md). Update it in the same commit
as each user-visible change. At release time, give the section a version and date
and start a fresh WIP section.

Git can provide a commit summary for review, for example:

```sh
git log --reverse --format='- %s (%h)' b5bd9a19..HEAD
```

Here `b5bd9a19` is this fork's upstream starting point; use the previous release
tag for later releases. Review that summary against the WIP notes. GitHub's
[generated release notes](https://docs.github.com/en/repositories/releasing-projects-on-github/automatically-generated-release-notes)
summarize merged pull requests, contributors and a comparison link. Since this
fork also uses direct commits, keep the maintained WIP notes as the user-facing
feature summary. A future release can use a prepared notes file with
`gh release create TAG --repo Hans-Einar/wmaker --verify-tag --draft --notes-file FILE`;
replace `TAG` and `FILE` with the release's existing tag and prepared notes.

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

### Command buttons (toggle actions)

For a persistent service such as CopyQ, create a command button:

```sh
wmappicon --command 'copyq --start-server toggle' --name copyq-toggle --icon copyq
wmappicon -s
```

Each activation runs the command, even when CopyQ is already running. The icon
uses a separate `WMCommandButton` identity and is saved with `Forced = Yes` and
`BuggyApplication = Yes`. It does not bind to the application's window or wait
for a new window to appear. Activation follows Window Maker's single/double-click
preference. Reusing `--name` updates the command on the existing button in the
current Clip. `--icon` accepts an absolute filename or an icon name.

Commands use Window Maker's normal argument parsing, not a shell. Do not append
`&`; use an explicit `sh -c` command if shell operators are required.
The optional sixth launcher IPC field is `command` for this mode. Existing
five-field requests retain ordinary application-launcher behavior.

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

## Minimize to Hide and app-icon toggling

```sh
wdwrite WindowMaker MinimizeHidesApplication YES
wdwrite WindowMaker AppIconTogglesHide YES
```

Both preferences default to `NO`. Install the new build and restart Window Maker
from its menu once. `MinimizeHidesApplication` sends normal minimization through
Hide Application: all windows in the application group are hidden without adding
individual miniature icons. This covers the titlebar minimize button and the
normal window-menu, shortcut and client minimization path.

Hide is used only when the application has a viewable application icon. Without
one (including an unmapped icon or a collapsed Clip), only the active window is
miniaturized and can be restored from its miniature icon. Enabling this preference
does not manufacture application icons for ungrouped clients. Existing launcher
matching and explicit `NoAppIcon` / `EmulateAppIcon` rules remain in effect.
The Hide shortcut also uses this safe fallback when the preference is enabled.
Upstream defaults bind Hide to Alt+H and Miniaturize to Alt+M; to bind Miniaturize
to Alt+H explicitly, use:

```sh
wdwrite WindowMaker HideKey None
wdwrite WindowMaker MiniaturizeKey 'Mod1+h'
```

`AppIconTogglesHide` makes plain left activation of an existing application icon
hide a visible application. Activating a hidden application retains the existing
unhide/workspace behavior. It applies to free application icons, Dock, Clip and
drawer launchers and follows `SingleClickLaunch`. In single-click mode, the
second click of a double-click does not immediately undo the first toggle.
Ctrl/Shift/modifier actions retain their existing meanings. Native dockapps and
command buttons keep their own actions; startup icons are not hidden mid-launch.
An app visible only on another workspace is activated by switching to its last
workspace and raising its windows. Clicking again while its windows are visible
on the current workspace hides it. Activating a hidden app returns to its last
workspace as before.

Validation: `python3 tests/application-hide.py` uses private Xvfb sessions and
real X11 clients to check grouped/ungrouped applications, free/Dock/Clip icons,
workspace switching, hide/unhide, double-click handling, absence of extra
miniature icons, disabled preferences and explicit opt-outs. Requires a built
source tree, Xvfb, a C compiler, libX11 development files and libXtst.
