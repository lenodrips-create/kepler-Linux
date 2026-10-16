#!/bin/bash
# .comp: a minute of random text, line after line, over the whole screen.
# The same command as in the KeplerOS Terminal. Esc or Ctrl+C stops it early.

if [ -z "$KEPLER_COMP" ] && [ -n "$DISPLAY" ]; then
    # a full-screen window of its own; this terminal waits for it
    KEPLER_COMP=1 xterm -fullscreen -title .comp -fa "DejaVu Sans Mono" -fs 10 \
        -bg black -fg "#33ff66" +sb -e "$0"
    printf '\e[32m.comp finished.\e[0m\n'
    exit
fi

words=(kernel segment inode socket handshake payload cipher vector buffer thread mutex page frame packet
       route token session daemon module driver sector cluster checksum entropy nonce certificate process
       registry cache stack)
shades=(46 40 34 157)
hex() { local s=""; for ((i = 0; i < $1; i += 4)); do printf -v s '%s%04x' "$s" $RANDOM; done; printf '%s' "${s:0:$1}"; }
word() { printf '%s' "${words[RANDOM % ${#words[@]}]}"; }

line() {
    local cols=$1 out="" i
    case $((RANDOM % 7)) in
    0) out="0x$(hex 8):"; while ((${#out} < cols - 6)); do out+="  $(hex 4)"; done ;;
    1) printf -v out '[%6d.%06d] %s: %s %s at 0x%s' $SECONDS $((RANDOM * 30 % 1000000)) "$(word)" "$(word)" \
           "$( ((RANDOM & 1)) && echo mapped || echo verified)" "$(hex 16)" ;;
    2) out='scan  /'; for ((i = 0; i < 2 + RANDOM % 5; i++)); do out+="$(word)/"; done
       out="${out%/}.ko   $((RANDOM * 271)) bytes   OK" ;;
    3) out="key $(hex 64)  $(hex 64)" ;;
    4) for ((i = 0; i < 18; i++)); do
           local b=""; for ((k = 0; k < 8; k++)); do b+=$((RANDOM & 1)); done; out+="$b "
       done ;;
    5) local pct=$((RANDOM % 101)) bar=""
       for ((i = 0; i < 40; i++)); do ((i * 100 / 40 < pct)) && bar+='#' || bar+='.'; done
       printf -v out '%-12s [%s] %3d%%' "$(word)" "$bar" $pct ;;
    *) local n=$((20 + RANDOM % (cols - 20)))
       out=$(LC_ALL=C tr -dc '!-~' </dev/urandom | head -c $n) ;;
    esac
    printf '%s' "${out:0:cols}"
}

trap 'printf "\e[0m\e[?25h"; exit' INT TERM
printf '\e[?25l\e[2J\e[H'
end=$((SECONDS + 60))
while ((SECONDS < end)); do
    cols=$(tput cols)
    printf '\e[38;5;%dm%s\e[0m\n' "${shades[RANDOM % 4]}" "$(line "$cols")"
    # the time left, in the corner
    printf '\e7\e[1;%dH\e[30;42m .comp  %02d s  [Esc] \e[0m\e8' $((cols - 20)) $((end - SECONDS))
    if read -rsn1 -t 0.03 key && [ "$key" = $'\e' ]; then
        break
    fi
done
printf '\e[0m\e[?25h'
