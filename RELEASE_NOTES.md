# Fork release notes

## WIP — Unreleased

These notes cover this fork's additions to upstream Window Maker. They describe
implemented changes on `master`; this is not a published release.

### Added

- **Live Clip launchers:** `wmappicon FILE.desktop` creates an omnipresent
  application icon without starting the application or leaving a helper running.
  Icons appear immediately in the current workspace.
- **Icon-only saving:** `wmappicon -s` / `--save-icon-state` saves Dock, Clip and
  drawer layouts while preserving the existing saved application session. It
  does not capture all currently open terminal windows.
- **Repeatable command buttons:** `wmappicon --command 'copyq --start-server toggle'
  --name copyq-toggle --icon copyq` creates a button that runs the command on
  every activation. Reusing the same name updates its command in the current Clip.
- **Clip-aware maximization:** `NoWindowOverClip = YES` reserves space beside
  visible Clip icons, including omnipresent icons, with a four-pixel gap. The
  calculation follows their current positions and collapsed state. This option
  defaults to `NO`.
- **Alternative resize gesture:** Ctrl plus the configured Window Maker modifier
  (usually Alt) and left-drag resizes a window.
- **Command-line help:** `wmappicon --help` / `-h` documents launcher creation,
  command buttons and saving.

### Fixed

- Clip launchers can bind to applications such as Chrome and ChatGPT that lack
  an application leader, using Window Maker's application-icon emulation.
  Activating a bound launcher uses normal application/workspace activation.
- Startup shading clears when the application is attached; the idle `...`
  indicator returns when the application closes.
- Omnipresent launchers are registered with the Clip's workspace machinery,
  and empty components of saved window-class identities survive save/restore.
- Icon-only saving no longer passes a null state to the workspace serializer.
- Desktop-file keys are read from the `[Desktop Entry]` section, and multiline
  launcher fields are rejected to protect the newline-delimited IPC format.

### Upgrade and usage notes

- Install the patched Window Maker and `wmappicon` tools together, then restart
  Window Maker from its menu. The new code requires this one-time restart.
- Enable Clip reservation with `wdwrite WindowMaker NoWindowOverClip YES`, then
  maximize windows again. Already maximized windows are not automatically resized
  when icons move. Fullscreen and explicit `FullMaximize` retain their behavior.
- Keep `SaveSessionOnExit = NO` if you only want to save icon layouts explicitly.
  Existing saved application-session entries are preserved, not removed.
- Command buttons use normal Window Maker argument parsing. Shell operators
  require an explicit `sh -c` command; appending `&` does not background a command.
- Class inference remains heuristic when desktop files omit `StartupWMClass`.
  Desktop Entry features such as `Terminal=true` and D-Bus activation are not
  implemented. Launcher IPC is asynchronous and concurrent requests are not
  supported. See [README.md](README.md) for details.

### Validation

- Built successfully; launcher behavior was tested with Chrome and ChatGPT in
  isolated Xvfb sessions and confirmed in the desktop session.
- Command buttons were tested with repeated counter-script activation and CopyQ
  toggling in an isolated session, including saved command-button attributes.
- `python3 tests/clip-maximize.py` covers ten actual maximization scenarios:
  enabled/disabled reservation, all four edges, collapsed Clip, workspace
  switching, omnipresent icons, `FullMaximize`, and disabled Clip.

<!-- Add user-visible changes here in the same commit as the implementation.
At release time, rename the WIP heading to the chosen version and date, archive
that section here, and start a new WIP section above it. Do not list plans as
implemented features. -->
