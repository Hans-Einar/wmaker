#!/bin/sh
# Requires Xvfb, libXtst development files, and (optionally) xcompmgr.
# Usage: test/dockpreview-xvfb.sh [left|right] [composite|plain] [dock|clip|clip-auto|drawer] [plain|effects]
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run=$(mktemp -d /tmp/wmaker-dockpreview.XXXXXX)
wm_pid= compositor_pid= xvfb_pid=
cleanup() {
    for pid in "$wm_pid" "$compositor_pid" "$xvfb_pid"; do
        if [ -n "$pid" ]; then kill "$pid" 2>/dev/null || :; fi
    done
    echo "Test artifacts: $run"
}
trap cleanup EXIT HUP INT TERM
side=${1:-left}
layout=${3:-dock}
position=0
[ "$side" != right ] || position=576
mkdir -p "$run/Defaults" "$run/Library/WindowMaker"
printf '#!/bin/sh\nexit 0\n' > "$run/Library/WindowMaker/autostart"
chmod +x "$run/Library/WindowMaker/autostart"
case "$layout" in
dock)
    wm_option=--no-clip
cat > "$run/Defaults/WMState" <<STATE
{
 Dock = {
  Position = "$position,0";
  Lowered = No;
  Applications = (
   { Command = true; Name = Logo.WMDock; Position = "0,0"; Lock = Yes; },
   { Command = true; Name = previewtest.PreviewTest; Position = "0,1"; Lock = Yes; }
  );
 };
 Workspaces = ({ Name = Main; }, { Name = Second; });
}
STATE
    ;;
clip|clip-auto)
    wm_option=--no-dock
    auto=No
    [ "$layout" != clip-auto ] || auto=Yes
    position=4
    [ "$side" != right ] || position=572
    cat > "$run/Defaults/WMState" <<STATE
{
 Clip = { Command = "-"; Name = Logo.WMClip; Position = "$position,0"; };
 Workspaces = (
  { Name = Main; Clip = {
    Lowered = No; AutoCollapse = $auto; AutoRaiseLower = $auto; Collapsed = No;
    Applications = (
      { Command = true; Name = previewtest.PreviewTest; Position = "0,1"; Lock = Yes; Omnipresent = Yes; }
    );
  }; },
  { Name = Second; Clip = { Lowered = No; AutoCollapse = $auto; AutoRaiseLower = $auto; }; }
 );
}
STATE
    ;;
drawer)
    wm_option=--no-clip
    cat > "$run/Defaults/WMState" <<STATE
{
 Dock = {
  Position = "$position,0"; Lowered = No;
  Applications = ({ Command = true; Name = Logo.WMDock; Position = "0,0"; Lock = Yes; });
 };
 Drawers = ({ Name = PreviewDrawer; Position = "$position,64";
  Dock = { AutoCollapse = Yes; Applications = (
   { Command = true; Name = previewtest.PreviewTest; Position = "1,0"; Lock = Yes; }
  ); };
 });
 Workspaces = ({ Name = Main; }, { Name = Second; });
}
STATE
    ;;
*) echo "Unknown layout: $layout" >&2; exit 2 ;;
esac
animations=YES
explosion=NO
if [ "${4:-plain}" = effects ]; then
    animations=NO
    explosion=YES
fi
cat > "$run/Defaults/WindowMaker" <<DEFAULTS
{
 DockWindowDrawer = YES;
 DisableAnimations = $animations;
 DockWindowDrawerExplosion = $explosion;
 WorkspaceNameDisplayPosition = center;
 WorkspacePager = NO;
 FocusMode = manual;
 IconSize = 64;
 ClipAutoexpandDelay = 100;
 ClipAutocollapseDelay = 200;
 ClipAutoraiseDelay = 100;
 ClipAutolowerDelay = 200;
}
DEFAULTS
cc -Wall -Wextra "$repo/test/dockpreview_test.c" -o "$run/test" -lX11 -lXtst
Xvfb -displayfd 3 -screen 0 640x480x24 -nolisten tcp 3>"$run/display" >"$run/xvfb.log" 2>&1 &
xvfb_pid=$!
i=0
while [ ! -s "$run/display" ]; do
    i=$((i + 1)); [ "$i" -lt 50 ] || exit 1
    sleep .1
done
DISPLAY=:$(cat "$run/display")
export DISPLAY
WMAKER_USER_ROOT="$run" "$repo/src/wmaker" "$wm_option" --no-autolaunch --for-real >"$run/wmaker.log" 2>&1 &
wm_pid=$!
sleep 1
if [ "${2:-composite}" = composite ]; then
    xcompmgr -a >"$run/compositor.log" 2>&1 &
    compositor_pid=$!
    sleep .3
    kill -0 "$compositor_pid"
fi
kill -0 "$wm_pid"
"$run/test" "$side" "$run/drawer.ppm" "$layout" "${4:-plain}"
kill -0 "$wm_pid"
if grep -E 'internal X error|fatal error|segmentation fault' "$run/wmaker.log"; then
    echo "Window Maker reported an error" >&2
    exit 1
fi
