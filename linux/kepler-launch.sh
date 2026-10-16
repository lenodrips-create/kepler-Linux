#!/bin/sh
# Starts the Kepler browser. Chromium's sandbox needs unprivileged user
# namespaces; where the kernel refuses them Kepler runs without it rather
# than not at all.
if ! unshare -Ur true 2>/dev/null; then
    echo "kepler: user namespaces unavailable, starting without the Chromium sandbox"
    export QTWEBENGINE_DISABLE_SANDBOX=1
fi
/usr/bin/Kepler "$@"
echo "kepler: exited with status $?"
