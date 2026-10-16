#!/bin/sh
# The X session: pick a sensible screen size, then hand the screen to the
# KeplerOS shell, which is the desktop and the window manager.

# Virtual machines often start at 1024x768; real screens report their own
# size, which is kept unless it is very large (the shell is not scaled).
OUT=$(xrandr 2>/dev/null | awk '/ connected/ { print $1; exit }')
CUR=$(xrandr 2>/dev/null | awk '/\*/ { print $1; exit }')
W=${CUR%%x*}
if [ -n "$OUT" ] && [ -n "$W" ] && { [ "$W" -lt 1280 ] || [ "$W" -gt 2560 ]; }; then
    if systemd-detect-virt -q; then
        TRY="1600x900 1680x1050 1440x900 1920x1080 1366x768 1280x800 1280x720"
    else
        TRY="1920x1080 1920x1200 2560x1440 1680x1050 1600x900 1440x900 1366x768 1280x800 1280x720"
    fi
    for m in $TRY; do
        if xrandr | grep -q "^ *$m "; then
            xrandr --output "$OUT" --mode "$m" && break
        fi
    done
fi

xset s off -dpms 2>/dev/null

# Kepler (Chromium) draws without the GPU inside virtual machines, where 3D is slow or missing
if systemd-detect-virt -q; then
    export QTWEBENGINE_CHROMIUM_FLAGS="--disable-gpu"
fi
export QT_QPA_PLATFORM=xcb

# booted with "kepdebug": mirror the session's and the programs' logs to the serial port
if grep -qw kepdebug /proc/cmdline && [ -w /dev/ttyS0 ]; then
    touch /tmp/kepler-apps.log
    { echo "kepdebug: session for $(id -un), virt=$(systemd-detect-virt), $(xrandr | grep '\*' | head -1)"; \
      echo "kepdebug: userns_restrict=$(cat /proc/sys/kernel/apparmor_restrict_unprivileged_userns 2>&1)"; \
      unshare -Ur true && echo "kepdebug: user namespaces work"; \
      tail -n +1 -F /tmp/kepler-apps.log; } > /dev/ttyS0 2>&1 &
    /usr/lib/kepleros/kepler-shell 2>&1 | tee /dev/ttyS0
    exit
fi

exec /usr/lib/kepleros/kepler-shell
