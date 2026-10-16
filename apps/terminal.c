/* Command Prompt: an 80x25 console with a small shell over the RAM file system. */

#include "apps.h"
#include "ramfs.h"

#define COLS       80
#define ROWS       25
#define SCROLLBACK 400
#define CW         8
#define LH         16
#define PAD        4

typedef struct {
    char ch;
    uint8_t at;
} cell_t;

typedef struct {
    cell_t lines[SCROLLBACK][COLS];
    int count; /* lines in use */
    int cx, cy;
    uint8_t attr, def_attr;
    bool esc;
    int view; /* lines scrolled up from the bottom */

    char input[256];
    int in_len, in_cur;
    int px, py; /* where the input starts */
    char hist[20][256];
    int hist_n, hist_pos;

    char cwd[128];
    char *cap; /* output redirection */
    int cap_len, cap_size;
    bool capturing;
    scrollbar_t sb;
} term_t;

static const uint32_t palette[16] = {
    0x000000, 0x000080, 0x008000, 0x008080, 0x800000, 0x800080, 0x808000, 0xC0C0C0,
    0x808080, 0x0000FF, 0x00FF00, 0x00FFFF, 0xFF0000, 0xFF00FF, 0xFFFF00, 0xFFFFFF,
};

/* VGA order is BGR-ish; map to the familiar cmd.exe colours */
static uint32_t pal(int i)
{
    static const int map[16] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
    return HEX(palette[map[i & 15]]);
}

/* ---- console output ------------------------------------------------------------------------------- */

static void clear_line(term_t *t, int y)
{
    for (int x = 0; x < COLS; x++)
        t->lines[y][x] = (cell_t){ ' ', t->def_attr };
}

static void newline(term_t *t)
{
    t->cx = 0;
    t->cy++;
    if (t->cy >= SCROLLBACK) {
        memmove(&t->lines[0], &t->lines[1], sizeof(t->lines[0]) * (SCROLLBACK - 1));
        t->cy = SCROLLBACK - 1;
        clear_line(t, t->cy);
        t->py--;
    }
    if (t->cy >= t->count) {
        t->count = t->cy + 1;
        clear_line(t, t->cy);
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    c = ktolower(c);
    return c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static void tputc(term_t *t, char c)
{
    if (t->capturing) {
        if (c == '\x1b' || t->esc) {
            t->esc = c == '\x1b';
            return;
        }
        if (t->cap_len + 1 >= t->cap_size) {
            t->cap_size = t->cap_size * 2 + 256;
            t->cap = krealloc(t->cap, t->cap_size);
        }
        t->cap[t->cap_len++] = c;
        return;
    }
    if (t->esc) {
        t->esc = false;
        int v = hexval(c);
        t->attr = v >= 0 ? (t->def_attr & 0xF0) | v : t->def_attr;
        return;
    }
    switch (c) {
    case '\x1b':
        t->esc = true;
        return;
    case '\n':
        newline(t);
        return;
    case '\r':
        t->cx = 0;
        return;
    case '\t':
        do
            tputc(t, ' ');
        while (t->cx % 8);
        return;
    }
    if (t->cx >= COLS)
        newline(t);
    t->lines[t->cy][t->cx++] = (cell_t){ c, t->attr };
}

static void tputs(term_t *t, const char *s)
{
    while (*s)
        tputc(t, *s++);
}

static void tprintf(term_t *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void tprintf(term_t *t, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    tputs(t, buf);
}

static void cls(term_t *t)
{
    t->count = 1;
    t->cx = t->cy = 0;
    clear_line(t, 0);
    t->view = 0;
}

/* ---- paths ------------------------------------------------------------------------------------------ */

static void to_dos(const char *path, char *out, int n)
{
    strlcpy(out, "C:", n);
    if (!strcmp(path, "/")) {
        strlcat(out, "\\", n);
        return;
    }
    int len = strlen(out);
    for (const char *p = path; *p && len < n - 1; p++)
        out[len++] = *p == '/' ? '\\' : *p;
    out[len] = 0;
}

static void from_user(term_t *t, const char *arg, char *out, int n)
{
    char tmp[256];
    const char *a = arg;
    if ((a[0] == 'c' || a[0] == 'C') && a[1] == ':')
        a += 2;
    int i = 0;
    for (; *a && i < 255; a++)
        tmp[i++] = *a == '\\' ? '/' : *a;
    tmp[i] = 0;
    if (!tmp[0])
        strlcpy(tmp, "/", sizeof tmp);
    fs_resolve(t->cwd, tmp, out, n);
}

static void prompt(term_t *t)
{
    char dos[160];
    to_dos(t->cwd, dos, sizeof dos);
    t->attr = t->def_attr;
    tprintf(t, "%s>", dos);
    t->px = t->cx;
    t->py = t->cy;
    t->in_len = t->in_cur = 0;
    t->input[0] = 0;
}

/* redraw the input line in the buffer after an edit */
static void render_input(term_t *t)
{
    /* clear from the prompt to the end of the buffer */
    for (int y = t->py; y < t->count; y++)
        for (int x = (y == t->py ? t->px : 0); x < COLS; x++)
            t->lines[y][x] = (cell_t){ ' ', t->def_attr };
    t->count = t->py + 1;
    t->cx = t->px;
    t->cy = t->py;
    t->attr = t->def_attr;
    for (int i = 0; i < t->in_len; i++)
        tputc(t, t->input[i]);
}

static void caret_pos(term_t *t, int *x, int *y)
{
    int off = t->px + t->in_cur;
    *x = off % COLS;
    *y = t->py + off / COLS;
}

/* ---- helpers --------------------------------------------------------------------------------------- */

static void commas(uint64_t v, char *out, int n)
{
    char tmp[32];
    ksnprintf(tmp, sizeof tmp, "%llu", (unsigned long long)v);
    int len = strlen(tmp), o = 0;
    for (int i = 0; i < len && o < n - 1; i++) {
        out[o++] = tmp[i];
        if ((len - i - 1) % 3 == 0 && i != len - 1 && o < n - 1)
            out[o++] = ',';
    }
    out[o] = 0;
}

static void stamp(const datetime_t *d, char *out, int n)
{
    int h12 = d->hour % 12 ? d->hour % 12 : 12;
    ksnprintf(out, n, "%02d/%02d/%04d  %02d:%02d %s", d->month, d->day, d->year, h12, d->minute, d->hour < 12 ? "AM" : "PM");
}

static void uptime_str(char *out, int n)
{
    uint32_t s = timer_ms() / 1000;
    ksnprintf(out, n, "%u hours, %u mins, %u secs", s / 3600, (s / 60) % 60, s % 60);
}

/* ---- expression evaluator for `calc` ---------------------------------------------------------------------- */

typedef struct {
    const char *p;
    bool err;
} expr_t;

static double parse_expr(expr_t *e);

static void skip(expr_t *e)
{
    while (*e->p == ' ')
        e->p++;
}

static double parse_atom(expr_t *e)
{
    skip(e);
    if (*e->p == '(') {
        e->p++;
        double v = parse_expr(e);
        skip(e);
        if (*e->p == ')')
            e->p++;
        else
            e->err = true;
        return v;
    }
    if (*e->p == '-') {
        e->p++;
        return -parse_atom(e);
    }
    if (!kisdigit(*e->p) && *e->p != '.') {
        e->err = true;
        return 0;
    }
    double v = 0, scale = 0;
    while (kisdigit(*e->p) || *e->p == '.') {
        if (*e->p == '.')
            scale = 1;
        else {
            v = v * 10 + (*e->p - '0');
            if (scale)
                scale *= 10;
        }
        e->p++;
    }
    return scale ? v / scale : v;
}

static double parse_term(expr_t *e)
{
    double v = parse_atom(e);
    for (;;) {
        skip(e);
        char op = *e->p;
        if (op != '*' && op != '/' && op != '%')
            return v;
        e->p++;
        double r = parse_atom(e);
        if (op == '*')
            v *= r;
        else if (r == 0)
            e->err = true;
        else if (op == '/')
            v /= r;
        else
            v -= r * (double)(int64_t)(v / r);
    }
}

static double parse_expr(expr_t *e)
{
    double v = parse_term(e);
    for (;;) {
        skip(e);
        char op = *e->p;
        if (op != '+' && op != '-')
            return v;
        e->p++;
        double r = parse_term(e);
        v = op == '+' ? v + r : v - r;
    }
}

static void fmt_double(double v, char *out, int n)
{
    bool neg = v < 0;
    if (neg)
        v = -v;
    uint64_t ip = (uint64_t)v;
    uint64_t frac = (uint64_t)((v - (double)ip) * 1000000.0 + 0.5);
    if (frac >= 1000000) {
        ip++;
        frac -= 1000000;
    }
    if (!frac) {
        ksnprintf(out, n, "%s%llu", neg ? "-" : "", (unsigned long long)ip);
        return;
    }
    char f[8];
    ksnprintf(f, sizeof f, "%06llu", (unsigned long long)frac);
    int len = 6;
    while (len > 0 && f[len - 1] == '0')
        f[--len] = 0;
    ksnprintf(out, n, "%s%llu.%s", neg ? "-" : "", (unsigned long long)ip, f);
}

/* ---- commands ------------------------------------------------------------------------------------------------ */

static const struct {
    const char *name, *help;
} help_list[] = {
    { "calc", "Evaluate an expression, e.g. calc (2+3)*7" },
    { "cd", "Change the current directory" },
    { "cls", "Clear the screen" },
    { "color", "Set console colours, e.g. color 0a" },
    { "cowsay", "Let a cow say something" },
    { "date", "Show today's date" },
    { "del", "Delete a file" },
    { "dir", "List a directory (ls for the short form)" },
    { "echo", "Print text; 'echo hi > file.txt' writes a file" },
    { "edit", "Open a file in Notepad" },
    { "exit", "Close this window" },
    { "help", "Show this list" },
    { "history", "Show previous commands" },
    { "lspci", "List PCI devices" },
    { "mem", "Show memory usage" },
    { "mkdir", "Create a directory" },
    { "neofetch", "Show system information with style" },
    { "ren", "Rename a file: ren old new" },
    { "rmdir", "Remove a directory" },
    { "shutdown", "Turn off the computer (/r restarts)" },
    { "start", "Start a program: start paint" },
    { "tasklist", "List running programs (taskkill <pid>)" },
    { "time", "Show the current time" },
    { "tree", "Show the folder structure" },
    { "type", "Print a text file" },
    { "uptime", "Time since boot" },
    { "ver", "Show the KeplerOS version" },
};

static void cmd_dir(term_t *t, const char *arg, bool brief)
{
    char path[128];
    from_user(t, arg[0] ? arg : ".", path, sizeof path);
    fnode_t *d = fs_lookup(path);
    if (!d) {
        tputs(t, "File Not Found\n");
        return;
    }
    if (brief) {
        for (fnode_t *f = d->dir ? d->child : d; f; f = d->dir ? f->next : NULL) {
            tprintf(t, "%s%s%s  ", f->dir ? "\x1b" "9" : "", f->name, f->dir ? "\x1bx" : "");
            if (!d->dir)
                break;
        }
        tputs(t, "\n");
        return;
    }
    char dos[160];
    to_dos(path, dos, sizeof dos);
    tputs(t, " Volume in drive C is KEPLER\n Volume Serial Number is 4B50-4C52\n\n");
    tprintf(t, " Directory of %s\n\n", dos);
    int files = 0, dirs = 0;
    size_t bytes = 0;
    char when[32], num[32];
    if (d->parent || d == fs_root()) {
        stamp(&d->mtime, when, sizeof when);
        tprintf(t, "%s    <DIR>          .\n", when);
        tprintf(t, "%s    <DIR>          ..\n", when);
        dirs += 2;
    }
    for (fnode_t *f = d->child; f; f = f->next) {
        stamp(&f->mtime, when, sizeof when);
        if (f->dir) {
            tprintf(t, "%s    <DIR>          %s\n", when, f->name);
            dirs++;
        } else {
            commas(f->size, num, sizeof num);
            tprintf(t, "%s    %14s %s\n", when, num, f->name);
            files++;
            bytes += f->size;
        }
    }
    commas(bytes, num, sizeof num);
    tprintf(t, "%16d File(s) %14s bytes\n", files, num);
    commas(heap_total() - heap_used(), num, sizeof num);
    tprintf(t, "%16d Dir(s)  %14s bytes free\n", dirs, num);
}

static void tree(term_t *t, fnode_t *d, char *prefix, int depth)
{
    for (fnode_t *f = d->child; f; f = f->next) {
        bool last = !f->next;
        tprintf(t, "%s%s%s%s\n", prefix, last ? "\\---" : "+---", f->dir ? "\x1b" "b" : "", f->name);
        tputs(t, "\x1bx");
        if (f->dir && depth < 6) {
            int len = strlen(prefix);
            strlcat(prefix, last ? "    " : "|   ", 128);
            tree(t, f, prefix, depth + 1);
            prefix[len] = 0;
        }
    }
}

static void neofetch(term_t *t)
{
    static const char *art[] = {
        "\x1b" "a            .-~~~-.",
        "\x1b" "a         .'`       `'.",
        "\x1b" "e   .~~~~\x1b" "a/    \x1b" "fK E P\x1b" "a    \\\x1b" "e~~~~.",
        "\x1b" "e  (    \x1b" "a|     \x1b" "fL E R\x1b" "a     |\x1b" "e    )",
        "\x1b" "e   '~~~~\x1b" "a\\      \x1b" "fO S\x1b" "a     /\x1b" "e~~~~'",
        "\x1b" "a         '.         .'",
        "\x1b" "a           '-.___.-'     \x1b" "b*",
        "",
        "",
        "",
        "",
    };
    char lines[11][96];
    char up[48], mem[48], res[32];
    uptime_str(up, sizeof up);
    ksnprintf(mem, sizeof mem, "%uMB / %uMB", (unsigned)(heap_used() >> 20), g_sys.mem_kb / 1024);
    ksnprintf(res, sizeof res, "%dx%d", g_sys.screen_w, g_sys.screen_h);
    ksnprintf(lines[0], 96, "\x1b" "a%s\x1bx@\x1b" "akeplerpc", g_user_name);
    ksnprintf(lines[1], 96, "-----------------");
    ksnprintf(lines[2], 96, "\x1b" "bOS\x1bx: KeplerOS %s (build %s) i686", KEPLER_VERSION, KEPLER_BUILD);
    ksnprintf(lines[3], 96, "\x1b" "bKernel\x1bx: kepler %s, written in C", KEPLER_VERSION);
    ksnprintf(lines[4], 96, "\x1b" "bUptime\x1bx: %s", up);
    ksnprintf(lines[5], 96, "\x1b" "bShell\x1bx: kepler-cmd");
    ksnprintf(lines[6], 96, "\x1b" "bResolution\x1bx: %s", res);
    ksnprintf(lines[7], 96, "\x1b" "bWM\x1bx: Kepler Big Sur");
    ksnprintf(lines[8], 96, "\x1b" "bCPU\x1bx: %.38s", g_sys.cpu_brand);
    ksnprintf(lines[9], 96, "\x1b" "bMemory\x1bx: %s", mem);
    lines[10][0] = 0;
    tputs(t, "\n");
    for (int i = 0; i < 11; i++) {
        tputs(t, art[i]);
        int vis = 0;
        for (const char *p = art[i]; *p; p++) {
            if (*p == '\x1b') {
                p++;
                continue;
            }
            vis++;
        }
        while (vis++ < 31)
            tputc(t, ' ');
        tputs(t, "\x1bx  ");
        tputs(t, lines[i]);
        tputs(t, "\x1bx\n");
    }
    /* colour swatches */
    tputs(t, "                                ");
    for (int c = 0; c < 16; c++) {
        uint8_t saved = t->attr;
        t->attr = (uint8_t)(c << 4) | (c & 15);
        tputs(t, "  ");
        t->attr = saved;
        if (c == 7) {
            tputs(t, "\n                                ");
        }
    }
    tputs(t, "\n\n");
}

static void cowsay(term_t *t, const char *msg)
{
    if (!msg[0])
        msg = "Moo! KeplerOS rocks.";
    int n = strlen(msg);
    tputs(t, " ");
    for (int i = 0; i < n + 2; i++)
        tputc(t, '_');
    tprintf(t, "\n< %s >\n ", msg);
    for (int i = 0; i < n + 2; i++)
        tputc(t, '-');
    tputs(t, "\n        \\   ^__^\n         \\  (oo)\\_______\n            (__)\\       )\\/\\\n                ||----w |\n                ||     ||\n");
}

static void run(term_t *t, window_t *w, char *line);

static void exec(term_t *t, window_t *w, char *cmd, char *arg)
{
    char path[128];
    for (char *p = cmd; *p; p++)
        *p = ktolower(*p);

    if (!strcmp(cmd, "help") || !strcmp(cmd, "?")) {
        tputs(t, "For more information on a specific command, just try it.\n\n");
        for (int i = 0; i < ARRAY_LEN(help_list); i++)
            tprintf(t, "\x1b" "e%-10s\x1bx%s\n", help_list[i].name, help_list[i].help);
    } else if (!strcmp(cmd, "cls") || !strcmp(cmd, "clear")) {
        cls(t);
    } else if (!strcmp(cmd, "ver")) {
        tprintf(t, "\nKeplerOS [Version %s.%s]\n\n", KEPLER_VERSION, KEPLER_BUILD);
    } else if (!strcmp(cmd, "echo")) {
        tprintf(t, "%s\n", arg[0] ? arg : "ECHO is on.");
    } else if (!strcmp(cmd, "dir")) {
        cmd_dir(t, arg, false);
    } else if (!strcmp(cmd, "ls")) {
        cmd_dir(t, arg, true);
    } else if (!strcmp(cmd, "cd") || !strcmp(cmd, "chdir")) {
        if (!arg[0]) {
            char dos[160];
            to_dos(t->cwd, dos, sizeof dos);
            tprintf(t, "%s\n", dos);
            return;
        }
        from_user(t, arg, path, sizeof path);
        fnode_t *d = fs_lookup(path);
        if (d && d->dir) {
            fs_path(d, t->cwd, sizeof t->cwd); /* canonical case */
        } else {
            tputs(t, "The system cannot find the path specified.\n");
        }
    } else if (!strcmp(cmd, "type") || !strcmp(cmd, "cat")) {
        from_user(t, arg, path, sizeof path);
        fnode_t *f = fs_lookup(path);
        if (!f || f->dir) {
            tputs(t, "The system cannot find the file specified.\n");
        } else if (!strcasecmp(fs_ext(f->name), "jpg")) {
            tputs(t, "That is a picture. Try: start viewer\n");
        } else {
            for (size_t i = 0; i < f->size; i++)
                tputc(t, f->data[i]);
            if (f->size && f->data[f->size - 1] != '\n')
                tputc(t, '\n');
        }
    } else if (!strcmp(cmd, "mkdir") || !strcmp(cmd, "md")) {
        from_user(t, arg, path, sizeof path);
        if (!arg[0] || fs_lookup(path) || !fs_create(path, true))
            tputs(t, "A subdirectory or file already exists, or the name is invalid.\n");
        desktop_refresh_icons();
    } else if (!strcmp(cmd, "del") || !strcmp(cmd, "rm") || !strcmp(cmd, "erase")) {
        from_user(t, arg, path, sizeof path);
        fnode_t *f = fs_lookup(path);
        if (!f || f->dir)
            tputs(t, "Could Not Find the file.\n");
        else
            fs_remove(f);
        desktop_refresh_icons();
    } else if (!strcmp(cmd, "rmdir") || !strcmp(cmd, "rd")) {
        from_user(t, arg, path, sizeof path);
        fnode_t *f = fs_lookup(path);
        if (!f || !f->dir || f == fs_root())
            tputs(t, "The system cannot find the path specified.\n");
        else if (f->child)
            tputs(t, "The directory is not empty.\n");
        else
            fs_remove(f);
    } else if (!strcmp(cmd, "ren") || !strcmp(cmd, "rename") || !strcmp(cmd, "mv")) {
        char *sp = strchr(arg, ' ');
        if (!sp) {
            tputs(t, "The syntax of the command is incorrect.\n");
            return;
        }
        *sp = 0;
        from_user(t, arg, path, sizeof path);
        fnode_t *f = fs_lookup(path);
        if (!f || !fs_rename(f, sp + 1))
            tputs(t, "The system cannot find the file specified.\n");
        desktop_refresh_icons();
    } else if (!strcmp(cmd, "edit") || !strcmp(cmd, "notepad")) {
        if (arg[0]) {
            from_user(t, arg, path, sizeof path);
            notepad_open(path);
        } else {
            notepad_open(NULL);
        }
    } else if (!strcmp(cmd, "start")) {
        ui_launch(arg[0] ? arg : "cmd");
    } else if (!strcmp(cmd, "time")) {
        datetime_t d;
        rtc_read(&d);
        tprintf(t, "The current time is: %02d:%02d:%02d\n", d.hour, d.minute, d.second);
    } else if (!strcmp(cmd, "date")) {
        datetime_t d;
        rtc_read(&d);
        static const char *days[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
        tprintf(t, "The current date is: %s %02d/%02d/%04d\n", days[d.weekday], d.month, d.day, d.year);
    } else if (!strcmp(cmd, "mem") || !strcmp(cmd, "free")) {
        char a[32], b[32], c[32];
        commas(heap_total(), a, sizeof a);
        commas(heap_used(), b, sizeof b);
        commas(heap_total() - heap_used(), c, sizeof c);
        tprintf(t, "\n    Physical memory:  %u KB\n", g_sys.mem_kb);
        tprintf(t, "    Kernel heap:      %s bytes\n    Used:             %s bytes\n    Free:             %s bytes\n", a, b, c);
        tprintf(t, "    RAM disk:         %u bytes in files\n\n", (unsigned)fs_used_bytes());
    } else if (!strcmp(cmd, "uptime")) {
        char up[48];
        uptime_str(up, sizeof up);
        tprintf(t, "up %s\n", up);
    } else if (!strcmp(cmd, "tasklist") || !strcmp(cmd, "ps")) {
        tputs(t, "\nImage Name                     PID  Window Title\n");
        tputs(t, "========================= ======== ====================================\n");
        tputs(t, "kernel                           0  (KeplerOS kernel)\n");
        tputs(t, "shell                            1  (desktop, taskbar)\n");
        for (window_t *x = wm_first(); x; x = x->next)
            if (!x->dead)
                tprintf(t, "%-25.25s %8d  %.36s\n", x->title, x->id + 100, x->title);
        tputs(t, "\n");
    } else if (!strcmp(cmd, "taskkill") || !strcmp(cmd, "kill")) {
        const char *a = arg;
        while (*a && !kisdigit(*a))
            a++;
        int pid = katoi(a) - 100;
        for (window_t *x = wm_first(); x; x = x->next)
            if (!x->dead && x->id == pid) {
                tprintf(t, "SUCCESS: The process with PID %d has been terminated.\n", pid + 100);
                wm_close(x);
                return;
            }
        tprintf(t, "ERROR: The process \"%s\" not found.\n", arg);
    } else if (!strcmp(cmd, "uname")) {
        tprintf(t, "KeplerOS keplerpc %s.%s i686\n", KEPLER_VERSION, KEPLER_BUILD);
    } else if (!strcmp(cmd, "whoami")) {
        tprintf(t, "keplerpc\\%s\n", g_user_name);
    } else if (!strcmp(cmd, "hostname")) {
        tputs(t, "KEPLERPC\n");
    } else if (!strcmp(cmd, "neofetch") || !strcmp(cmd, "sysinfo") || !strcmp(cmd, "systeminfo")) {
        neofetch(t);
    } else if (!strcmp(cmd, "lspci")) {
        char lines[32][64];
        int n = pci_list(lines, 32);
        for (int i = 0; i < n; i++)
            tprintf(t, "%s\n", lines[i]);
    } else if (!strcmp(cmd, "calc")) {
        expr_t e = { arg, false };
        double v = parse_expr(&e);
        skip(&e);
        if (e.err || *e.p || !arg[0]) {
            tputs(t, "Usage: calc <expression>, e.g. calc (2+3)*7/2\n");
        } else {
            char out[48];
            fmt_double(v, out, sizeof out);
            tprintf(t, "%s\n", out);
        }
    } else if (!strcmp(cmd, "color")) {
        int v = (hexval(arg[0]) << 4) | hexval(arg[1]);
        if (strlen(arg) != 2 || hexval(arg[0]) < 0 || hexval(arg[1]) < 0 || (v >> 4) == (v & 15)) {
            t->def_attr = 0x07;
        } else {
            t->def_attr = (uint8_t)v;
        }
        for (int y = 0; y < t->count; y++)
            for (int x = 0; x < COLS; x++)
                t->lines[y][x].at = t->def_attr;
        t->attr = t->def_attr;
        w->bg = pal(t->def_attr >> 4);
    } else if (!strcmp(cmd, "history")) {
        for (int i = 0; i < t->hist_n; i++)
            tprintf(t, "%3d  %s\n", i + 1, t->hist[i]);
    } else if (!strcmp(cmd, "tree")) {
        char prefix[128] = "";
        char dos[160];
        to_dos(t->cwd, dos, sizeof dos);
        tprintf(t, "Folder PATH listing for volume KEPLER\n%s\n", dos);
        tree(t, fs_lookup(t->cwd), prefix, 0);
    } else if (!strcmp(cmd, "cowsay")) {
        cowsay(t, arg);
    } else if (!strcmp(cmd, "beep")) {
        ui_beep(880, 150);
    } else if (!strcmp(cmd, "exit")) {
        wm_close(w);
    } else if (!strcmp(cmd, "shutdown")) {
        ui_power(strchr(arg, 'r') || strchr(arg, 'R') ? 2 : 1);
    } else if (!strcmp(cmd, "reboot")) {
        ui_power(2);
    } else if (!strcmp(cmd, "logoff")) {
        ui_logoff();
    } else {
        tprintf(t, "'%s' is not recognized as an internal or external command,\noperable program or batch file.\n", cmd);
    }
}

static void run(term_t *t, window_t *w, char *line)
{
    while (*line == ' ')
        line++;
    if (!*line)
        return;
    /* redirection: cmd > file or cmd >> file */
    char *gt = strchr(line, '>');
    char target[128] = "";
    bool append = false;
    if (gt) {
        append = gt[1] == '>';
        char *f = gt + (append ? 2 : 1);
        while (*f == ' ')
            f++;
        from_user(t, f, target, sizeof target);
        *gt = 0;
        for (char *e = gt - 1; e >= line && *e == ' '; e--)
            *e = 0;
        t->capturing = true;
        t->cap_len = 0;
    }
    char *sp = strchr(line, ' ');
    char *arg = (char *)"";
    if (sp) {
        *sp = 0;
        arg = sp + 1;
        while (*arg == ' ')
            arg++;
    }
    /* "cd.." and "cd\" work like in cmd.exe */
    if (ktolower(line[0]) == 'c' && ktolower(line[1]) == 'd' && (line[2] == '.' || line[2] == '\\')) {
        arg = line + 2;
        line[2] = 0;
    }
    exec(t, w, line, arg);
    if (gt) {
        t->capturing = false;
        fnode_t *f = fs_lookup(target);
        if (!f)
            f = fs_create(target, false);
        if (!f || f->dir) {
            tputs(t, "The system cannot find the path specified.\n");
        } else if (append) {
            fs_append(f, t->cap, t->cap_len);
        } else {
            fs_write(f, t->cap, t->cap_len);
        }
        desktop_refresh_icons();
    }
}

/* ---- window ---------------------------------------------------------------------------------------------- */

static int first_line(term_t *t) { return MAX(0, MAX(0, t->count - ROWS) - t->view); }

static void term_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    term_t *t = w->data;
    int first = first_line(t);
    uint8_t def_bg = t->def_attr >> 4;
    for (int r = 0; r < ROWS; r++) {
        int line = first + r;
        if (line >= t->count)
            break;
        int y = PAD + r * LH;
        for (int x = 0; x < COLS; x++) {
            cell_t c = t->lines[line][x];
            if ((c.at >> 4) != def_bg)
                gfx_fill(g, PAD + x * CW, y, CW, LH, pal(c.at >> 4));
            if (c.ch != ' ')
                gfx_text_n(g, &font_mono, PAD + x * CW, y - 1, &c.ch, 1, pal(c.at & 15));
        }
    }
    if (t->view == 0 && wm_active() == w && (timer_ms() / 530) % 2 == 0) {
        int x, y;
        caret_pos(t, &x, &y);
        int row = y - first;
        if (row >= 0 && row < ROWS)
            gfx_fill(g, PAD + x * CW, PAD + row * LH + LH - 4, CW, 3, pal(t->def_attr & 15));
    }
    t->sb.total = MAX(t->count, ROWS);
    t->sb.visible = ROWS;
    t->sb.pos = t->sb.total - ROWS - t->view;
    scrollbar_paint(&t->sb, g, cw - SCROLLBAR_W, 0, ch);
}

static void after_key(window_t *w)
{
    term_t *t = w->data;
    t->view = 0;
    wm_invalidate_client(w);
}

static void insert_text(term_t *t, const char *s, int n)
{
    for (int i = 0; i < n && s[i] && s[i] != '\n'; i++) {
        if (t->in_len >= (int)sizeof t->input - 1)
            break;
        memmove(&t->input[t->in_cur + 1], &t->input[t->in_cur], t->in_len - t->in_cur);
        t->input[t->in_cur++] = s[i];
        t->in_len++;
    }
    t->input[t->in_len] = 0;
    render_input(t);
}

static void complete(term_t *t)
{
    /* complete the last word against entries of its directory */
    int start = t->in_cur;
    while (start > 0 && t->input[start - 1] != ' ')
        start--;
    char word[128];
    int wl = t->in_cur - start;
    memcpy(word, &t->input[start], wl);
    word[wl] = 0;
    char dirpart[128] = "", base[128];
    char *slash = strrchr(word, '\\');
    if (!slash)
        slash = strrchr(word, '/');
    if (slash) {
        memcpy(dirpart, word, slash - word + 1);
        dirpart[slash - word + 1] = 0;
        strlcpy(base, slash + 1, sizeof base);
    } else {
        strlcpy(base, word, sizeof base);
    }
    char dpath[128];
    from_user(t, dirpart[0] ? dirpart : ".", dpath, sizeof dpath);
    fnode_t *d = fs_lookup(dpath);
    if (!d || !d->dir)
        return;
    int bl = strlen(base);
    for (fnode_t *f = d->child; f; f = f->next) {
        bool match = true;
        for (int i = 0; i < bl; i++)
            if (ktolower(f->name[i]) != ktolower(base[i]))
                match = false;
        if (match) {
            insert_text(t, f->name + bl, strlen(f->name + bl));
            return;
        }
    }
}

static void term_key(window_t *w, const input_event_t *ev)
{
    term_t *t = w->data;
    if (!ev->pressed)
        return;
    if (ev->mods & MOD_CTRL) {
        switch (ktolower(ev->key)) {
        case 'c':
            render_input(t);
            tputs(t, "^C\n");
            prompt(t);
            break;
        case 'l':
            cls(t);
            prompt(t);
            break;
        case 'v': {
            const char *c = clipboard_get();
            insert_text(t, c, strlen(c));
            break;
        }
        default:
            return;
        }
        after_key(w);
        return;
    }
    switch (ev->key) {
    case KEY_PGUP:
        t->view = MIN(t->view + ROWS / 2, MAX(0, t->count - ROWS));
        wm_invalidate_client(w);
        return;
    case KEY_PGDN:
        t->view = MAX(0, t->view - ROWS / 2);
        wm_invalidate_client(w);
        return;
    case KEY_ENTER: {
        char line[256];
        strlcpy(line, t->input, sizeof line);
        t->in_cur = t->in_len;
        render_input(t);
        newline(t);
        if (line[0] && (t->hist_n == 0 || strcmp(t->hist[t->hist_n - 1], line))) {
            if (t->hist_n == 20) {
                memmove(t->hist[0], t->hist[1], sizeof t->hist[0] * 19);
                t->hist_n--;
            }
            strlcpy(t->hist[t->hist_n++], line, sizeof t->hist[0]);
        }
        t->hist_pos = t->hist_n;
        run(t, w, line);
        if (!w->dead) {
            if (t->cx)
                newline(t);
            if (line[0] && strcmp(line, "cls") && strcmp(line, "clear"))
                newline(t);
            prompt(t);
        }
        break;
    }
    case KEY_BACKSPACE:
        if (t->in_cur > 0) {
            memmove(&t->input[t->in_cur - 1], &t->input[t->in_cur], t->in_len - t->in_cur + 1);
            t->in_cur--;
            t->in_len--;
            render_input(t);
        }
        break;
    case KEY_DELETE:
        if (t->in_cur < t->in_len) {
            memmove(&t->input[t->in_cur], &t->input[t->in_cur + 1], t->in_len - t->in_cur);
            t->in_len--;
            render_input(t);
        }
        break;
    case KEY_LEFT:
        if (t->in_cur > 0)
            t->in_cur--;
        break;
    case KEY_RIGHT:
        if (t->in_cur < t->in_len)
            t->in_cur++;
        break;
    case KEY_HOME: t->in_cur = 0; break;
    case KEY_END: t->in_cur = t->in_len; break;
    case KEY_ESC:
        t->in_len = t->in_cur = 0;
        t->input[0] = 0;
        render_input(t);
        break;
    case KEY_UP:
    case KEY_DOWN:
        if (!t->hist_n)
            break;
        t->hist_pos = CLAMP(t->hist_pos + (ev->key == KEY_UP ? -1 : 1), 0, t->hist_n);
        strlcpy(t->input, t->hist_pos < t->hist_n ? t->hist[t->hist_pos] : "", sizeof t->input);
        t->in_len = t->in_cur = strlen(t->input);
        render_input(t);
        break;
    case KEY_TAB:
        complete(t);
        break;
    default:
        if (ev->key >= 32 && ev->key < 127) {
            char c = (char)ev->key;
            insert_text(t, &c, 1);
        } else {
            return;
        }
    }
    after_key(w);
}

static void term_mouse(window_t *w, int kind, int x, int y, int button)
{
    term_t *t = w->data;
    rect_t c = wm_client_rect(w);
    if (kind == MS_WHEEL) {
        t->view = CLAMP(t->view - button * 3, 0, MAX(0, t->count - ROWS));
        wm_invalidate_client(w);
        return;
    }
    if (x >= c.w - SCROLLBAR_W || t->sb.drag_off >= 0) {
        t->sb.total = MAX(t->count, ROWS);
        t->sb.visible = ROWS;
        t->sb.pos = t->sb.total - ROWS - t->view;
        if (scrollbar_mouse(&t->sb, kind, x, y, c.w - SCROLLBAR_W, 0, c.h)) {
            t->view = t->sb.total - ROWS - t->sb.pos;
            wm_invalidate_client(w);
        }
        return;
    }
    if (kind == MS_DOWN && button == MOUSE_RIGHT) {
        const char *clip = clipboard_get();
        insert_text(t, clip, strlen(clip));
        after_key(w);
    }
}

static void term_tick(window_t *w)
{
    if (wm_active() == w)
        wm_invalidate_client(w);
}

static void term_close(window_t *w)
{
    term_t *t = w->data;
    kfree(t->cap);
    kfree(t);
}

static const window_ops_t term_ops = { term_paint, term_key, term_mouse, term_tick, term_close, NULL };

window_t *terminal_open(void)
{
    term_t *t = kzalloc(sizeof *t);
    t->def_attr = t->attr = 0x07;
    t->sb.drag_off = -1;
    strlcpy(t->cwd, "/Documents", sizeof t->cwd);
    cls(t);
    tprintf(t, "KeplerOS [Version %s.%s]\n", KEPLER_VERSION, KEPLER_BUILD);
    tputs(t, "(C) Copyright 2026 The KeplerOS Project.\n\n");
    prompt(t);
    window_t *w = wm_create("kepler -- cmd -- 80x25", ICON_CMD, COLS * CW + 2 * PAD + SCROLLBAR_W, ROWS * LH + 2 * PAD,
                            &term_ops, t, WF_NO_MAX);
    wm_set_app(w, "Terminal", NULL, NULL, NULL);
    w->bg = HEX(0x000000);
    wm_set_tick(w, 530);
    return w;
}
