#!/bin/sh
# The Terminal: a real bash shell in xterm, in KeplerOS's colours, with the
# KeplerOS commands added (/etc/kepleros/commands.sh).
cd "$HOME"
export KEPLER_TERMINAL=1
exec xterm -title Terminal -class Terminal \
    -fa "DejaVu Sans Mono" -fs 11 -geometry 100x36 \
    -bg "#0c0c0c" -fg "#e6e6e6" -cr "#40e66e" -bc \
    -sl 5000 +sb -si -sk \
    -xrm "XTerm*selectToClipboard: true" -xrm "XTerm*metaSendsEscape: true" \
    -xrm "XTerm*VT100.translations: #override Ctrl Shift <Key>C: copy-selection(CLIPBOARD) \n Ctrl Shift <Key>V: insert-selection(CLIPBOARD)" \
    "$@" -e bash -l
