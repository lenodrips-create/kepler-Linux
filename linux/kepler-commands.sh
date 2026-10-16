# KeplerOS commands in bash: the old KeplerOS Terminal's commands, next to
# every Linux command. Read by interactive shells (from /etc/bash.bashrc).
# Where Linux has a command of the same name that does the same job (cd, echo,
# mkdir, rmdir, curl, date, uptime, history, exit...) the Linux one is used.

[ -n "$PS1" ] || return 0

_kepler_help() {
    cat <<'HELP'
KeplerOS commands (every Linux command works too):

  .comp      Fill the screen with random text for a minute
  calc       Evaluate an expression, e.g. calc (2+3)*7
  cd         Change the current directory
  cls        Clear the screen
  color      Set console colours, e.g. color 0a
  cowsay     Let a cow say something
  curl       Fetch a web page, e.g. curl https://example.com
  date       Show today's date
  del        Delete a file
  dir        List a directory (ls for the short form)
  echo       Print text; 'echo hi > file.txt' writes a file
  edit       Edit a text file
  exit       Close this window
  help       Show this list
  history    Show previous commands
  ipconfig   Show the network connection
  lspci      List PCI devices
  mem        Show memory usage
  mkdir      Create a directory
  neofetch   Show system information with style
  nslookup   Look up a host's address, e.g. nslookup wikipedia.org
  ren        Rename a file: ren old new
  rmdir      Remove a directory
  shutdown   Turn off the computer (/r restarts)
  start      Start a program: start kepler
  tasklist   List running programs (taskkill <pid>)
  time       Show the current time
  tree       Show the folder structure
  type       Print a text file
  uptime     Time since boot
  ver        Show the KeplerOS version
HELP
}
help() { if [ $# -eq 0 ]; then _kepler_help; else builtin help "$@"; fi; }

ver() { printf '\nKeplerOS [Version 1.0.2600]\n\n'; }
cls() { clear; }
dir() { ls -la "$@"; }
del() { rm "$@"; }
erase() { rm "$@"; }
ren() { mv "$@"; }
rename() { mv "$@"; }
md() { mkdir "$@"; }
rd() { rmdir "$@"; }
chdir() { cd "$@"; }
alias cd..='cd ..'
copy() { cp "$@"; }
move() { mv "$@"; }
edit() { nano "$@"; }
notepad() { nano "$@"; }
beep() { printf '\a'; }
wget() { curl -L "$@"; }
# "type file" prints the file; "type command" still says what the command is
type() { if [ $# -eq 1 ] && [ -f "$1" ]; then cat "$1"; else builtin type "$@"; fi; }

# "time" alone tells the time; "time command" still times the command
_kepler_time() {
    if [ $# -eq 0 ]; then
        echo "The current time is: $(date +%H:%M:%S)"
    else
        time "$@"
    fi
}
alias time='_kepler_time '

_kepler_calc() {
    if [ -z "${*// /}" ]; then
        echo "Usage: calc <expression>, e.g. calc (2+3)*7/2"
        return 1
    fi
    awk "BEGIN { r = ($*); if (r == int(r)) printf \"%d\\n\", r; else print r }" 2>/dev/null ||
        echo "Usage: calc <expression>, e.g. calc (2+3)*7/2"
}
# bash would read "(2+3)*7" as its own syntax: the alias turns the expression into a comment
# and the function takes it, as typed, from the history
_kepler_calc_typed() {
    local line
    line=$(HISTTIMEFORMAT= builtin history 1)
    case $line in
    *calc*) _kepler_calc "${line#*calc}" ;;
    *) _kepler_calc ;;
    esac
}
alias calc='_kepler_calc_typed #'

# color BF: background and text colour, one hex digit each, as in cmd.exe; "color" alone resets
color() {
    local pal=(000000 000080 008000 008080 800000 800080 808000 c0c0c0
               808080 0000ff 00ff00 00ffff ff0000 ff00ff ffff00 ffffff)
    local a=${1,,}
    if [[ ! $a =~ ^[0-9a-f]{2}$ ]] || [ "${a:0:1}" = "${a:1:1}" ]; then
        printf '\e]111\a\e]110\a'
        return
    fi
    printf '\e]11;#%s\a\e]10;#%s\a' "${pal[16#${a:0:1}]}" "${pal[16#${a:1:1}]}"
}

cowsay() {
    local msg="${*:-Moo! Welcome to KeplerOS.}" line
    printf -v line '%*s' $((${#msg} + 2)) ''
    printf ' %s\n< %s >\n %s\n' "${line// /_}" "$msg" "${line// /-}"
    cat <<'COW'
        \   ^__^
         \  (oo)\_______
            (__)\       )\/\
                ||----w |
                ||     ||
COW
}

mem() {
    free -h
    echo
    df -h / /tmp 2>/dev/null
}

ipconfig() { ip -brief address show; ip route show default 2>/dev/null; }
command -v ifconfig >/dev/null || ifconfig() { ipconfig; }

command -v nslookup >/dev/null || nslookup() {
    if [ $# -eq 0 ]; then
        echo "usage: nslookup <host>"
        return 1
    fi
    local ip
    ip=$(getent ahostsv4 "$1" | awk 'NR == 1 { print $1 }')
    if [ -n "$ip" ]; then
        printf 'Name:     %s\nAddress:  %s\n' "$1" "$ip"
    else
        echo "Can't find $1."
        return 1
    fi
}

tasklist() { ps -eo pid,user,%cpu,%mem,comm --sort=-%cpu; }
taskkill() {
    local pid
    for pid in "$@"; do
        case $pid in /pid | /PID | /f | /F) continue ;; esac
        kill "$pid" && echo "SUCCESS: The process with PID $pid has been terminated."
    done
}

shutdown() {
    case "$*" in
    "") systemctl poweroff ;;
    /r | /R | -r) systemctl reboot ;;
    *) command shutdown "$@" ;;
    esac
}
logoff() { pkill -x kepler-shell; }

start() {
    case "${1,,}" in
    "") kepler-terminal >/dev/null 2>&1 & disown ;;
    kepler | browser | web | safari) shift; setsid Kepler "$@" >/dev/null 2>&1 & disown ;;
    cmd | terminal) kepler-terminal >/dev/null 2>&1 & disown ;;
    *) setsid "$@" >/dev/null 2>&1 & disown ;;
    esac
}

neofetch() {
    local g=$'\e[32m' w=$'\e[97m' c=$'\e[36m' x=$'\e[0m'
    local up mem cpu res
    up=$(uptime -p 2>/dev/null | sed 's/^up //')
    mem=$(free -m | awk '/^Mem:/ { print $3 "MB / " $2 "MB" }')
    cpu=$(awk -F': ' '/model name/ { print $2; exit }' /proc/cpuinfo)
    res=$(xrandr 2>/dev/null | awk '/\*/ { print $1; exit }')
    local art
    mapfile -t art <<'ART'
                                                          .
       .                                              .
                                   :,:,,,
                +++++++++#####*+==::::,:::,,:                .
             ++++%@@@%%%%%%%+:++++=-==,:::::-,-:    .
           ++* @@%%%%+=:...,-:=*++**+++++===+++=-++*+
          ++* @@%%%+....  ::-::-=+*****+*****+++-.%@@%+++
           +* @@%%%....  =:::+#*==--:----======+-..,=%%@@#++
            ++#@%%%*...  +***+:+==-:-----------::.....=%%%@%+++
             +++%@%%%=...=+****+++----==-------:,..  ...*%%%@#++
                ++#@@%%*=,:++*#**###*+---=++**=-,..  ....%%%@@ *+
                   +++%@@%%++=:==+**######**+=-,..  ....+%%%@@ *++.
                       +*+#*+@%%#++**---+++=-,......:=+%%%%@@ *++
                            +-:-##*+#@@@%%%%%%%%%%%%%%%@@@%++++
                               ....----:,:::#######+++++++++
                                   ......
ART
    local info=(
        "${g}$(id -un)${x}@${g}$(hostname)"
        "-----------------"
        "${c}OS${x}: KeplerOS 1.0 (build 2600) on $(. /etc/os-release; echo "$PRETTY_NAME")"
        "${c}Kernel${x}: Linux $(uname -r)"
        "${c}Uptime${x}: ${up}"
        "${c}Shell${x}: bash ${BASH_VERSION%%(*}"
        "${c}Resolution${x}: ${res:-unknown}"
        "${c}WM${x}: KeplerOS shell"
        "${c}CPU${x}: ${cpu:0:38}"
        "${c}Memory${x}: ${mem}")
    echo
    local aw=0 l
    for l in "${art[@]}"; do ((${#l} > aw)) && aw=${#l}; done
    if (($(tput cols 2>/dev/null || echo 80) >= aw + 52)); then
        # side by side, the information level with the planet
        local off=$(((${#art[@]} - ${#info[@]}) / 2)) i
        for i in "${!art[@]}"; do
            l=""
            ((i >= off)) && l=${info[i - off]:-}
            printf '%s%-*s%s  %s%s\n' "$w" "$aw" "${art[i]}" "$x" "$l" "$x"
        done
    else
        printf '%s%s%s\n' "$w" "$(printf '%s\n' "${art[@]}")" "$x"
        echo
        for i in "${!info[@]}"; do printf '  %s%s\n' "${info[i]}" "$x"; done
    fi
    printf '  '
    for i in 0 1 2 3 4 5 6 7; do printf '\e[4%dm   ' $i; done
    printf '%s\n\n' "$x"
}

# the greeting the KeplerOS Terminal had
if [ -n "$KEPLER_TERMINAL" ] && [ -z "$_KEPLER_GREETED" ]; then
    export _KEPLER_GREETED=1
    printf 'KeplerOS [Version 1.0.2600]\n(C) Copyright 2026 The KeplerOS Project.\nType help for the KeplerOS commands. Every Linux command works too.\n\n'
fi
