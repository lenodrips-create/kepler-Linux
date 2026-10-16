#!/bin/sh
# The Linux Terminal: a real bash shell in xterm, in KeplerOS's colours.
cd "$HOME"
exec xterm -title "Linux Terminal" -class KeplerTerminal \
    -fa "DejaVu Sans Mono" -fs 11 -geometry 100x30 \
    -bg "#0c0c0c" -fg "#e6e6e6" -cr "#40e66e" -bc \
    -sl 5000 +sb -si -sk \
    -xrm "XTerm*selectToClipboard: true" -xrm "XTerm*metaSendsEscape: true" \
    -xrm "XTerm*VT100.translations: #override Ctrl Shift <Key>C: copy-selection(CLIPBOARD) \n Ctrl Shift <Key>V: insert-selection(CLIPBOARD)" \
    "$@" -e bash -l
