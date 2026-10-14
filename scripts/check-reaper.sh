#!/usr/bin/env bash
# Verifies REAPER is set up to find the plugin and has it in its scan cache.
set -uo pipefail
CFG="$HOME/.config/REAPER"
OK=1

if [ -d "$HOME/.vst3/Axell Penta EQ.vst3" ]; then
  echo "[ok] Plugin installed in ~/.vst3"
else
  echo "[!!] Not installed in ~/.vst3 - run scripts/install.sh"; OK=0
fi

if [ -f "$CFG/reaper.ini" ]; then
  VSTPATH=$(grep -E '^vstpath(64)?=' "$CFG/reaper.ini" | head -n1 | cut -d= -f2- || true)
  if [ -z "$VSTPATH" ]; then
    echo "[ok] REAPER uses its default VST path (includes ~/.vst3)"
  else
    # exact match of ~/.vst3 (either spelling of the home path) in the ';'-separated list
    FOUND=0
    IFS=';' read -ra DIRS <<< "$VSTPATH"
    for d in "${DIRS[@]}"; do
      d="${d%/}"
      case "$d" in
        "$HOME/.vst3"|"/var$HOME/.vst3"|"${HOME#/var}/.vst3"|"~/.vst3") FOUND=1 ;;
      esac
    done
    if [ $FOUND -eq 1 ]; then
      echo "[ok] REAPER VST path includes ~/.vst3: $VSTPATH"
    else
      echo "[!!] REAPER VST path does not include ~/.vst3. Current value:"
      echo "     $VSTPATH"
      echo "     Fix: Options > Preferences > Plug-ins > VST > add ;/var/home/$USER/.vst3 to the path > Re-scan"; OK=0
    fi
  fi
else
  echo "[??] $CFG/reaper.ini not found (REAPER never started, or config elsewhere)"
fi

CACHE=$(ls "$CFG"/reaper-vstplugins*.ini 2>/dev/null | head -n1)
if [ -n "$CACHE" ] && grep -qi "axell.penta.eq" "$CACHE"; then
  echo "[ok] REAPER has scanned the plugin ($CACHE)"
else
  echo "[..] Not in REAPER's scan cache yet - restart REAPER or press Re-scan in Preferences > Plug-ins > VST"; OK=0
fi

[ $OK -eq 1 ] && echo "Ready: insert FX on a track and search 'Penta'." || exit 1
