/* BradyBuddy: Brady the fox teaches the Linux terminal. Lessons with commands to
 * try (each opens the Terminal with the command ready: press Up, then Enter),
 * a Command Finder, and a quiz. */

#include "apps.h"

#define SIDE_W   224
#define BRADY_W  249
#define BRADY_H  240
#define BUBBLE_W 270
#define PAD      22
#define BTN_H    28

extern const uint32_t brady_240_argb[];
static const image_t brady_img = { (uint32_t *)brady_240_argb, BRADY_W, BRADY_H };

#define C_SIDE_TOP HEX(0x6B3FD6)
#define C_SIDE_BOT HEX(0x3F238F)
#define C_BG       HEX(0xFBFAFF)
#define C_INK      HEX(0x1D1B2E)
#define C_SOFT     HEX(0x5E5A78)
#define C_PURPLE   HEX(0x5B33C9)
#define C_CARD     HEX(0xF1ECFF)
#define C_CMD_BG   HEX(0x1E1B2E)
#define C_CMD_FG   HEX(0x7CF0A0)
#define C_GOOD     HEX(0x1F9D55)
#define C_BAD      HEX(0xD13B3B)

/* ---- what Brady teaches ------------------------------------------------------------------------- */

typedef struct {
    const char *cmd, *what;
} cmd_t;

typedef struct {
    const char *title, *brady, *intro;
    cmd_t cmds[7];
    const char *try_it;
} lesson_t;

static const lesson_t lessons[] = {
    { "Hello, terminal!",
      "Hi, I'm Brady! The terminal looks scary, but it's just you typing to the computer. Let's start easy!",
      "The terminal lets you control the computer by typing commands. The line that ends in $ is the "
      "prompt: kepler is you, vm is the computer's name, and ~ means you are in your home folder. "
      "Type a command, press Enter, and the computer answers.",
      { { "pwd", "Print Working Directory: shows which folder you are in." },
        { "whoami", "Shows your user name." },
        { "echo Hello Brady", "Prints whatever you write after echo." },
        { "clear", "Wipes the screen clean (cls works too)." },
        { "history", "Lists the commands you have typed before." } },
      "Type pwd and press Enter. I bet it says /home/kepler!" },
    { "Moving around",
      "Folders inside folders inside folders... cd is how you hop between them. Like a fox through tunnels!",
      "Everything on Linux lives in one big tree of folders that starts at / (called root). ls looks "
      "around, cd moves you. A path that starts with / begins at the top of the tree; any other path "
      "starts from where you are now.",
      { { "ls", "Lists the files and folders where you are." },
        { "ls -la", "Lists everything, hidden files too, with sizes and dates." },
        { "cd /etc", "Goes into the /etc folder." },
        { "cd ..", "Goes up one folder." },
        { "cd ~", "Goes back to your home folder from anywhere (just cd works too)." },
        { "cd -", "Jumps back to the folder you were in before." } },
      "Go to the top with cd /, look around with ls, then come home with cd ~." },
    { "Making and deleting",
      "Careful with rm: there's no Trash in the terminal. Once it's gone, it's gone!",
      "You can make folders and files, copy them, move or rename them, and delete them. mv does both "
      "moving and renaming: renaming is just moving a file to a new name.",
      { { "mkdir practice", "Makes a new folder called practice." },
        { "touch notes.txt", "Makes an empty file called notes.txt." },
        { "cp notes.txt copy.txt", "Copies a file." },
        { "mv copy.txt renamed.txt", "Renames (or moves) a file." },
        { "rm renamed.txt", "Deletes a file. Forever!" },
        { "rm -r practice", "Deletes a folder and everything inside it." } },
      "Make a folder called practice, go into it with cd practice, and make a file with touch." },
    { "Reading and writing files",
      "Files are just text most of the time. cat shows them, nano lets you change them.",
      "There are many ways to look inside a file. cat prints the whole thing, less lets you scroll "
      "(press q to quit), head and tail show the start or the end. > writes text into a file and >> "
      "adds to the end of it.",
      { { "echo \"I love Linux\" > hello.txt", "Writes a line into hello.txt (replacing what was there)." },
        { "echo \"Me too\" >> hello.txt", "Adds a line to the end of hello.txt." },
        { "cat hello.txt", "Prints the whole file." },
        { "less /etc/os-release", "Shows a file you can scroll through. Press q to quit." },
        { "head -n 3 /etc/os-release", "Shows the first 3 lines." },
        { "nano hello.txt", "Edits a file. Ctrl+O saves, Ctrl+X quits." } },
      "Write your name into a file with echo, then read it back with cat." },
    { "Finding things",
      "Lost something? I can sniff out any file. Well, grep and find can. I just watch.",
      "grep searches for words inside files. find searches for files by their names. Both are super "
      "useful when you don't know where something is.",
      { { "grep Linux hello.txt", "Shows the lines of hello.txt that contain Linux." },
        { "grep -ri kepler /etc/hostname", "Searches without caring about capital letters (-i)." },
        { "find . -name \"*.txt\"", "Finds every .txt file in this folder and below." },
        { "which bash", "Shows where a program lives." },
        { "wc -l hello.txt", "Counts the lines in a file." } },
      "Use find . -name \"*.txt\" to find the files you made in the last lesson." },
    { "Pipes and chains",
      "This is where it gets fun: you can plug commands into each other like garden hoses!",
      "The | symbol (a pipe) sends what one command prints into the next command. && runs the second "
      "command only if the first one worked. Small commands joined together can do big jobs.",
      { { "ls /etc | wc -l", "Counts how many things are in /etc." },
        { "history | grep cd", "Shows only the cd commands you have typed." },
        { "ls -la | less", "Lets you scroll through a long list." },
        { "mkdir test && cd test", "Makes a folder, then goes into it if that worked." },
        { "cat /etc/passwd | sort | head -n 5", "Sorts a file and shows the first 5 lines." } },
      "Count the files in /usr/bin with ls /usr/bin | wc -l. There are a lot!" },
    { "sudo and apt",
      "sudo is like asking the computer politely to let you do grown-up stuff. With great power...",
      "sudo runs a command as the administrator (called root). apt is Ubuntu's app store for the "
      "terminal: it installs and removes programs. On KeplerOS, sudo doesn't ask for a password. On the "
      "live disc, programs you install are gone after a restart.",
      { { "sudo apt update", "Gets the latest list of programs. Do this first." },
        { "apt search cowsay", "Searches for programs." },
        { "sudo apt install sl", "Installs a program (try sl, then type sl!)." },
        { "sudo apt remove sl", "Removes a program." },
        { "sudo whoami", "Shows that sudo runs things as root." } },
      "Run sudo apt update, then install sl and type sl. Choo choo!" },
    { "Running programs",
      "Every program running is a process. Sometimes one gets stuck. Here's how to rescue it.",
      "ps lists the programs that are running and top shows them live (press q to quit). Each has a "
      "number, its PID. kill stops a program by its PID. Ctrl+C stops whatever is running in the "
      "terminal right now.",
      { { "ps aux", "Lists every running program." },
        { "top", "A live view of programs and memory. Press q to quit." },
        { "sleep 100", "Waits 100 seconds. Press Ctrl+C to stop it early!" },
        { "sleep 100 &", "Runs it in the background (the & at the end)." },
        { "jobs", "Shows your background programs." },
        { "kill %1", "Stops background job number 1." } },
      "Start sleep 100, then stop it with Ctrl+C." },
    { "Permissions",
      "Some files say 'keep out!' Linux checks who you are before letting you read or change things.",
      "ls -l shows letters like rwxr-xr-x: r is read, w is write, x is run (execute), for the owner, "
      "the group and everyone else. chmod changes them. A script needs x before you can run it.",
      { { "ls -l", "Shows permissions, owners and sizes." },
        { "echo 'echo Hi from my script' > hi.sh", "Makes a tiny script." },
        { "chmod +x hi.sh", "Makes it runnable." },
        { "./hi.sh", "Runs it (./ means 'in this folder')." },
        { "sudo chown root hi.sh", "Gives the file to root." } },
      "Make hi.sh, make it runnable with chmod +x, and run it with ./hi.sh." },
    { "Keyboard tricks",
      "Pro tip from a pro fox: never type a whole file name. Press Tab and let the computer do it!",
      "The fastest terminal users barely type. Tab finishes names for you, the Up arrow brings back old "
      "commands, and a few Ctrl keys do the rest.",
      { { "Tab", "Finishes a command or file name. Press twice to see choices." },
        { "Up / Down arrows", "Scroll through the commands you typed before." },
        { "Ctrl+C", "Stops the running command." },
        { "Ctrl+L", "Clears the screen." },
        { "Ctrl+R", "Searches your old commands as you type." },
        { "Ctrl+A / Ctrl+E", "Jumps to the start / end of the line." },
        { "ls --help", "Almost every command explains itself with --help." } },
      "Type cd /us and press Tab. Watch it finish to /usr/!" },
    { "KeplerOS extras",
      "These ones are special: they only exist on KeplerOS. My favourite is .comp!",
      "KeplerOS adds its own commands next to the Linux ones, including the ones from the old KeplerOS "
      "Terminal. Type help to see all of them.",
      { { "help", "Lists the KeplerOS commands." },
        { "neofetch", "Shows your system with the KeplerOS planet." },
        { ".comp", "Fills the screen with random text for a minute. Esc stops it." },
        { "cowsay Brady rules", "A cow says something. (Brady approves.)" },
        { "calc (2+3)*7", "A quick calculator." },
        { "dir", "Like ls -la, the KeplerOS way." } },
      "Run neofetch, then .comp, and feel like a movie hacker." },
};
#define N_LESSONS ARRAY_LEN(lessons)

typedef struct {
    const char *name, *what, *example;
} dict_t;

static const dict_t dict[] = {
    { "pwd", "Show the folder you are in", "pwd" },
    { "ls", "List files and folders", "ls -la" },
    { "cd", "Change folder", "cd /etc" },
    { "mkdir", "Make a folder", "mkdir photos" },
    { "rmdir", "Remove an empty folder", "rmdir photos" },
    { "touch", "Make an empty file (or update its date)", "touch notes.txt" },
    { "cp", "Copy files", "cp notes.txt backup.txt" },
    { "mv", "Move or rename files", "mv notes.txt todo.txt" },
    { "rm", "Delete files (no Trash!)", "rm todo.txt" },
    { "cat", "Print a file", "cat /etc/hostname" },
    { "less", "Scroll through a file (q quits)", "less /etc/os-release" },
    { "head", "First lines of a file", "head -n 5 /etc/passwd" },
    { "tail", "Last lines of a file", "tail -n 5 /etc/passwd" },
    { "nano", "Simple text editor (Ctrl+O save, Ctrl+X quit)", "nano notes.txt" },
    { "echo", "Print text", "echo Hello" },
    { "grep", "Search inside files", "grep root /etc/passwd" },
    { "find", "Search for files by name", "find ~ -name \"*.txt\"" },
    { "which", "Where a program lives", "which nano" },
    { "wc", "Count lines, words and letters", "wc -l /etc/passwd" },
    { "sort", "Sort lines", "sort names.txt" },
    { "uniq", "Remove repeated lines (after sort)", "sort names.txt | uniq" },
    { "tree", "Show folders as a tree", "tree -L 2 /etc" },
    { "du", "How much space a folder uses", "du -sh ~" },
    { "df", "Free disk space", "df -h" },
    { "free", "Free memory", "free -h" },
    { "uname", "About the Linux kernel", "uname -a" },
    { "whoami", "Your user name", "whoami" },
    { "hostname", "The computer's name", "hostname" },
    { "date", "Today's date and time", "date" },
    { "uptime", "How long the computer has been on", "uptime" },
    { "history", "Commands you typed before", "history" },
    { "clear", "Clear the screen", "clear" },
    { "sudo", "Run a command as the administrator", "sudo whoami" },
    { "apt", "Install and remove programs", "sudo apt install sl" },
    { "ps", "List running programs", "ps aux" },
    { "top", "Live view of programs (q quits)", "top" },
    { "kill", "Stop a program by its number", "kill 1234" },
    { "jobs", "Background programs of this terminal", "jobs" },
    { "chmod", "Change who may read, write or run a file", "chmod +x hi.sh" },
    { "chown", "Change who owns a file", "sudo chown kepler hi.sh" },
    { "ip", "Network addresses", "ip a" },
    { "curl", "Download a web page", "curl https://example.com" },
    { "lspci", "List the computer's devices", "lspci" },
    { "man", "Help for a command (or use --help)", "ls --help" },
    { "exit", "Close the terminal", "exit" },
    { "shutdown", "Turn the computer off", "shutdown" },
    { "help", "KeplerOS: list its commands", "help" },
    { "neofetch", "KeplerOS: system info with the planet", "neofetch" },
    { ".comp", "KeplerOS: a minute of hacker text", ".comp" },
    { "cowsay", "KeplerOS: a talking cow", "cowsay moo" },
    { "calc", "KeplerOS: calculator", "calc 12*12" },
    { "dir", "KeplerOS: list a folder in detail", "dir" },
    { "del", "KeplerOS: delete a file", "del notes.txt" },
    { "ren", "KeplerOS: rename a file", "ren a.txt b.txt" },
    { "ver", "KeplerOS: the version", "ver" },
};

typedef struct {
    const char *q, *a[4];
    int right;
    const char *why;
} quiz_t;

static const quiz_t quiz[] = {
    { "Which command shows the folder you are in?", { "ls", "pwd", "cd", "whoami" }, 1,
      "pwd = Print Working Directory." },
    { "How do you go up one folder?", { "cd ~", "up", "cd ..", "ls .." }, 2, "Two dots always mean the folder above." },
    { "Which command makes a new folder?", { "touch pics", "mkdir pics", "folder pics", "new pics" }, 1,
      "mkdir = make directory. touch makes files." },
    { "Which one deletes notes.txt for good?", { "trash notes.txt", "cut notes.txt", "rm notes.txt", "drop notes.txt" },
      2, "rm removes it. There's no Trash, so be careful!" },
    { "How do you install a program called sl?", { "install sl", "sudo apt install sl", "apt get sl", "sudo get sl" },
      1, "sudo gives permission, apt installs." },
    { "Which keys stop a command that is running?", { "Ctrl+C", "Ctrl+V", "Esc", "Alt+F4" }, 0,
      "Ctrl+C means cancel in the terminal." },
    { "Which key finishes a file name for you?", { "Shift", "Enter", "Tab", "Space" }, 2,
      "Tab completion saves tons of typing." },
    { "What does >> do in: echo hi >> notes.txt", { "Replaces the file", "Adds to the end of the file", "Runs it twice",
                                                     "Deletes the file" },
      1, "> replaces, >> adds to the end." },
    { "Which command prints what is inside a file?", { "cat", "dog", "show", "open" }, 0,
      "cat (short for concatenate) prints files." },
    { "Find the lines with 'kepler' in notes.txt:", { "find kepler notes.txt", "grep kepler notes.txt",
                                                      "look kepler notes.txt", "search notes.txt" },
      1, "grep searches inside files, find searches for file names." },
    { "What does ~ mean?", { "The top of the tree", "The previous folder", "Your home folder", "Nothing" }, 2,
      "~ is short for /home/kepler." },
    { "What makes a script runnable?", { "sudo run hi.sh", "chmod +x hi.sh", "chown +x hi.sh", "exec hi.sh" }, 1,
      "chmod +x adds the run permission." },
    { "Which command shows the running programs?", { "ls procs", "ps aux", "run list", "apps" }, 1,
      "ps lists processes. top shows them live." },
    { "What does the | (pipe) do?", { "Sends one command's output into another", "Comments out a line",
                                       "Makes a folder", "Quits the terminal" },
      0, "ls | wc -l: ls's list goes into wc to be counted." },
};
#define N_QUIZ ARRAY_LEN(quiz)

/* ---- the window -------------------------------------------------------------------------------------- */

enum { PAGE_FINDER = N_LESSONS, PAGE_QUIZ, N_PAGES };

enum { HIT_PAGE, HIT_CMD, HIT_BACK, HIT_NEXT, HIT_TERMINAL, HIT_ANSWER, HIT_QUIZ_NEXT, HIT_FIELD };

typedef struct {
    rect_t r;
    int kind, arg;
    const char *cmd;
} hit_t;

typedef struct {
    int page;
    int scroll, content_h;
    bool seen[N_LESSONS];
    char said[256]; /* Brady's reaction; empty: the page's own line */
    textfield_t search;
    int q, picked, score, answered;
    int order[N_QUIZ];
    hit_t hits[96];
    int n_hits, hover;
} brady_t;

static void add_hit(brady_t *b, rect_t r, int kind, int arg, const char *cmd)
{
    if (b->n_hits < ARRAY_LEN(b->hits))
        b->hits[b->n_hits++] = (hit_t){ r, kind, arg, cmd };
}

static const char *page_name(int p)
{
    if (p == PAGE_FINDER)
        return "Command Finder";
    if (p == PAGE_QUIZ)
        return "Quiz";
    return lessons[p].title;
}

static const char *brady_line(brady_t *b)
{
    if (b->said[0])
        return b->said;
    if (b->page < (int)N_LESSONS)
        return lessons[b->page].brady;
    if (b->page == PAGE_FINDER)
        return "Forgot a command? Type what you want to do, like 'copy' or 'delete', and I'll find it.";
    return "Quiz time! Pick an answer. No pressure... okay, a little pressure.";
}

static bool cmd_runnable(const char *cmd)
{
    /* key presses are explained, not run */
    return strncmp(cmd, "Tab", 3) && strncmp(cmd, "Up ", 3) && strncmp(cmd, "Ctrl", 4);
}

static void open_terminal(brady_t *b, const char *cmd)
{
#ifdef KEPLER_HOSTED
    if (cmd) {
        char arg[200];
        ksnprintf(arg, sizeof arg, "--suggest=%s", cmd);
        host_launch("kepler-terminal", arg);
        ksnprintf(b->said, sizeof b->said, "I opened the Terminal with  %s  ready. Press the Up arrow, then Enter!", cmd);
        return;
    }
#endif
    (void)b;
    ui_launch("terminal");
}

/* ---- painting ---------------------------------------------------------------------------------------------- */

static void paint_sidebar(brady_t *b, gfx_t *g, int ch)
{
    gfx_vgrad(g, 0, 0, SIDE_W, ch, C_SIDE_TOP, C_SIDE_BOT);
    gfx_text(g, &font_big_bold, 20, 18, "BradyBuddy", HEX(0xFFFFFF));
    gfx_text(g, &font_ui, 21, 50, "Learn the Linux terminal", RGBA(255, 255, 255, 200));
    int y = 82;
    for (int p = 0; p < N_PAGES; p++) {
        if (p == PAGE_FINDER) {
            gfx_hline(g, 16, y + 4, SIDE_W - 32, RGBA(255, 255, 255, 60));
            y += 12;
        }
        rect_t r = rect_make(10, y, SIDE_W - 20, 28);
        bool sel = p == b->page;
        if (sel)
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 7, RGBA(255, 255, 255, 235), CORNER_ALL);
        else if (b->hover >= 0 && b->hits[b->hover].kind == HIT_PAGE && b->hits[b->hover].arg == p)
            gfx_round_rect(g, r.x, r.y, r.w, r.h, 7, RGBA(255, 255, 255, 40), CORNER_ALL);
        uint32_t fg = sel ? C_PURPLE : HEX(0xFFFFFF);
        char label[64];
        if (p < (int)N_LESSONS)
            ksnprintf(label, sizeof label, "%d", p + 1);
        else
            strlcpy(label, p == PAGE_FINDER ? "?" : "!", sizeof label);
        gfx_circle(g, r.x + 17, r.y + 14, 10, sel ? C_PURPLE : RGBA(255, 255, 255, 50));
        gfx_text_centered(g, &font_ui_bold, r.x + 7, r.y + 7, 20, label, sel ? HEX(0xFFFFFF) : fg);
        gfx_text_clipped(g, sel ? &font_ui_bold : &font_ui, r.x + 36, r.y + 7, r.w - 60, page_name(p), fg);
        if (p < (int)N_LESSONS && b->seen[p] && !sel)
            gfx_text(g, &font_ui_bold, r.x + r.w - 20, r.y + 7, "v", HEX(0x9CF5B8));
        add_hit(b, r, HIT_PAGE, p, NULL);
        y += 30;
    }
    if (b->answered) {
        char s[48];
        ksnprintf(s, sizeof s, "Quiz score: %d / %d", b->score, b->answered);
        gfx_text(g, &font_ui, 20, ch - 30, s, RGBA(255, 255, 255, 210));
    }
}

static void paint_bubble(brady_t *b, gfx_t *g, int cw, int ch)
{
    const char *s = brady_line(b);
    int tw = BUBBLE_W - 28;
    int th = gfx_text_wrapped(NULL, &font_ui, 0, 0, tw, s, 0, 18);
    int bh = th + 40, bx = cw - BUBBLE_W - 16, by = MAX(18, ch - BRADY_H - 8 - bh - 4);
    gfx_shadow(g, bx, by, BUBBLE_W, bh, 14, 10, 3, 40);
    gfx_round_rect(g, bx, by, BUBBLE_W, bh, 14, HEX(0xFFFFFF), CORNER_ALL);
    gfx_round_frame(g, bx, by, BUBBLE_W, bh, 14, HEX(0xD9CCFF), CORNER_ALL);
    /* the tail, down towards Brady's head */
    int tx = cw - 112;
    for (int i = 0; i < 14; i++)
        gfx_hline(g, tx - 12 + i, by + bh - 1 + i, MAX(1, 14 - i), HEX(0xFFFFFF));
    gfx_text(g, &font_ui_bold, bx + 14, by + 10, "Brady", C_PURPLE);
    gfx_text_wrapped(g, &font_ui, bx + 14, by + 30, tw, s, C_INK, 18);
}

/* a command on a card: the command in terminal colours, what it does underneath */
static int paint_cmd(brady_t *b, gfx_t *g, int x, int y, int w, const char *cmd, const char *what, int oy)
{
    int th = gfx_text_wrapped(NULL, &font_ui, 0, 0, w - 24, what, 0, 17);
    int h = 46 + th - 17 + 8;
    rect_t r = rect_make(x, y, w, h);
    bool hot = b->hover >= 0 && b->hits[b->hover].kind == HIT_CMD && b->hits[b->hover].cmd == cmd;
    gfx_round_rect(g, x, y, w, h, 10, hot ? HEX(0xE6DCFF) : C_CARD, CORNER_ALL);
    int cwid = MIN(w - 24, text_width(&font_mono, cmd) + 20);
    gfx_round_rect(g, x + 10, y + 8, cwid, 24, 6, C_CMD_BG, CORNER_ALL);
    gfx_text_clipped(g, &font_mono, x + 20, y + 11, cwid - 20, cmd, C_CMD_FG);
    if (cmd_runnable(cmd)) {
        const char *t = "Try it >";
        gfx_text(g, &font_ui_bold, x + w - 12 - text_width(&font_ui_bold, t), y + 13, t, hot ? C_PURPLE : HEX(0x9A8CC8));
        add_hit(b, rect_make(r.x, r.y + oy, r.w, r.h), HIT_CMD, 0, cmd);
    }
    gfx_text_wrapped(g, &font_ui, x + 12, y + 38, w - 24, what, C_SOFT, 17);
    return h;
}

static int paint_lesson(brady_t *b, gfx_t *g, int w, int oy)
{
    const lesson_t *l = &lessons[b->page];
    int y = 0;
    char head[16];
    ksnprintf(head, sizeof head, "LESSON %d OF %d", b->page + 1, (int)N_LESSONS);
    gfx_text(g, &font_ui_bold, 0, y, head, HEX(0x9A8CC8));
    y += 20;
    gfx_text(g, &font_big_bold, 0, y, l->title, C_INK);
    y += 40;
    y += gfx_text_wrapped(g, &font_ui, 0, y, w, l->intro, C_INK, 19) + 14;
    for (int i = 0; i < ARRAY_LEN(l->cmds) && l->cmds[i].cmd; i++)
        y += paint_cmd(b, g, 0, y, w, l->cmds[i].cmd, l->cmds[i].what, oy) + 8;
    /* the practice task */
    y += 6;
    int th = gfx_text_wrapped(NULL, &font_ui, 0, 0, w - 28, l->try_it, 0, 18);
    gfx_round_rect(g, 0, y, w, th + 36, 10, HEX(0xE5F8EC), CORNER_ALL);
    gfx_text(g, &font_ui_bold, 14, y + 10, "Your turn!", C_GOOD);
    gfx_text_wrapped(g, &font_ui, 14, y + 28, w - 28, l->try_it, C_INK, 18);
    y += th + 36 + 10;
    return y;
}

static bool matches(const dict_t *d, const char *q)
{
    if (!q[0])
        return true;
    int n = strlen(q);
    const char *fields[3] = { d->name, d->what, d->example };
    for (int f = 0; f < 3; f++)
        for (const char *s = fields[f]; *s; s++) {
            int k = 0;
            while (k < n && s[k] && ktolower(s[k]) == ktolower(q[k]))
                k++;
            if (k == n)
                return true;
        }
    return false;
}

static int paint_finder(brady_t *b, gfx_t *g, int w, int oy)
{
    int y = 0;
    gfx_text(g, &font_big_bold, 0, y, "Command Finder", C_INK);
    y += 40;
    gfx_text(g, &font_ui, 0, y, "What do you want to do? Try: copy, delete, space, install, search...", C_SOFT);
    y += 24;
    textfield_paint(&b->search, g, 0, y, w, true);
    add_hit(b, rect_make(0, y + oy, w, 21), HIT_FIELD, 0, NULL);
    y += 34;
    int found = 0;
    for (int i = 0; i < ARRAY_LEN(dict); i++) {
        if (!matches(&dict[i], b->search.text))
            continue;
        char what[160];
        ksnprintf(what, sizeof what, "%s.  Example: %s", dict[i].what, dict[i].example);
        y += paint_cmd(b, g, 0, y, w, dict[i].example, what, oy) + 8;
        found++;
    }
    if (!found)
        gfx_text_wrapped(g, &font_ui, 0, y, w, "Hmm, I don't know that one. Try another word, or type a command name.", C_SOFT, 18);
    return y + 20;
}

static int paint_quiz(brady_t *b, gfx_t *g, int w, int oy)
{
    const quiz_t *q = &quiz[b->order[b->q]];
    int y = 0;
    char head[32];
    ksnprintf(head, sizeof head, "QUESTION %d OF %d", b->q + 1, (int)N_QUIZ);
    gfx_text(g, &font_ui_bold, 0, y, head, HEX(0x9A8CC8));
    y += 22;
    y += gfx_text_wrapped(g, &font_heading, 0, y, w, q->q, C_INK, 24) + 16;
    for (int i = 0; i < 4; i++) {
        rect_t r = rect_make(0, y, w, 40);
        uint32_t bg = HEX(0xFFFFFF), edge = HEX(0xD9CCFF), fg = C_INK;
        if (b->picked >= 0 && i == q->right) {
            bg = HEX(0xE5F8EC);
            edge = C_GOOD;
        } else if (b->picked == i) {
            bg = HEX(0xFDEBEB);
            edge = C_BAD;
        } else if (b->picked < 0 && b->hover >= 0 && b->hits[b->hover].kind == HIT_ANSWER && b->hits[b->hover].arg == i) {
            bg = C_CARD;
        }
        gfx_round_rect(g, r.x, r.y, r.w, r.h, 10, bg, CORNER_ALL);
        gfx_round_frame(g, r.x, r.y, r.w, r.h, 10, edge, CORNER_ALL);
        char letter[2] = { (char)('A' + i), 0 };
        gfx_circle(g, r.x + 22, r.y + 20, 11, edge == HEX(0xD9CCFF) ? C_PURPLE : edge);
        gfx_text_centered(g, &font_ui_bold, r.x + 11, r.y + 13, 22, letter, HEX(0xFFFFFF));
        gfx_text_clipped(g, &font_mono, r.x + 44, r.y + 11, r.w - 56, q->a[i], fg);
        if (b->picked < 0)
            add_hit(b, rect_make(r.x, r.y + oy, r.w, r.h), HIT_ANSWER, i, NULL);
        y += 48;
    }
    if (b->picked >= 0) {
        y += 4;
        rect_t r = rect_make(0, y, 170, BTN_H);
        gfx_round_rect(g, r.x, r.y, r.w, r.h, 8, C_PURPLE, CORNER_ALL);
        gfx_text_centered(g, &font_ui_bold, r.x, r.y + 7, r.w, b->q + 1 < (int)N_QUIZ ? "Next question" : "Start over",
                          HEX(0xFFFFFF));
        add_hit(b, rect_make(r.x, r.y + oy, r.w, r.h), HIT_QUIZ_NEXT, 0, NULL);
        y += BTN_H + 10;
    }
    return y;
}

static void pill_button(brady_t *b, gfx_t *g, int x, int y, int w, const char *label, int kind, bool primary)
{
    rect_t r = rect_make(x, y, w, BTN_H);
    bool hot = b->hover >= 0 && b->hits[b->hover].kind == kind;
    uint32_t bg = primary ? (hot ? HEX(0x4A26B0) : C_PURPLE) : (hot ? HEX(0xE6DCFF) : C_CARD);
    gfx_round_rect(g, r.x, r.y, r.w, r.h, 8, bg, CORNER_ALL);
    gfx_text_centered(g, &font_ui_bold, r.x, r.y + 7, r.w, label, primary ? HEX(0xFFFFFF) : C_PURPLE);
    add_hit(b, r, kind, 0, NULL);
}

static void br_paint(window_t *w, gfx_t *g, int cw, int ch)
{
    brady_t *b = w->data;
    b->n_hits = 0;
    gfx_fill(g, 0, 0, cw, ch, C_BG);
    paint_sidebar(b, g, ch);

    /* the page, scrolled, between the sidebar and Brady */
    int cx = SIDE_W + PAD, cy = 20;
    int cwid = cw - SIDE_W - PAD - BUBBLE_W - 30;
    int view_h = ch - cy - BTN_H - 28;
    gfx_t pg = gfx_sub(g, cx, cy, cwid, view_h);
    int oy = cy - b->scroll;
    gfx_t sg = gfx_sub(&pg, 0, -b->scroll, cwid, view_h + b->scroll);
    int first_hit = b->n_hits;
    if (b->page < (int)N_LESSONS)
        b->content_h = paint_lesson(b, &sg, cwid, oy);
    else if (b->page == PAGE_FINDER)
        b->content_h = paint_finder(b, &sg, cwid, oy);
    else
        b->content_h = paint_quiz(b, &sg, cwid, oy);
    /* hits are in client coordinates; keep only what is visible */
    for (int i = first_hit; i < b->n_hits; i++) {
        b->hits[i].r.x += cx;
        if (b->hits[i].r.y + b->hits[i].r.h < cy || b->hits[i].r.y > cy + view_h)
            b->hits[i].r.h = 0;
    }
    if (b->content_h > view_h) {
        /* a slim scroll indicator */
        int bar = MAX(30, view_h * view_h / b->content_h);
        int by = cy + (view_h - bar) * b->scroll / MAX(1, b->content_h - view_h);
        gfx_round_rect(g, cx + cwid + 6, by, 5, bar, 2, RGBA(90, 60, 180, 90), CORNER_ALL);
    }

    /* buttons along the bottom */
    int by = ch - BTN_H - 14;
    if (b->page > 0)
        pill_button(b, g, cx, by, 90, "< Back", HIT_BACK, false);
    if (b->page < N_PAGES - 1)
        pill_button(b, g, cx + 98, by, 90, "Next >", HIT_NEXT, false);
    pill_button(b, g, cx + cwid - 150, by, 150, "Open Terminal", HIT_TERMINAL, true);

    /* Brady and what he says */
    paint_bubble(b, g, cw, ch);
    gfx_blit_alpha(g, cw - BRADY_W - 6, ch - BRADY_H - 8, &brady_img, 255);
}

/* ---- input ----------------------------------------------------------------------------------------------- */

static void shuffle_quiz(brady_t *b)
{
    for (int i = 0; i < N_QUIZ; i++)
        b->order[i] = i;
    for (int i = N_QUIZ - 1; i > 0; i--) {
        int j = krand() % (i + 1);
        int t = b->order[i];
        b->order[i] = b->order[j];
        b->order[j] = t;
    }
    b->q = 0;
    b->picked = -1;
}

static void go(brady_t *b, int page)
{
    b->page = CLAMP(page, 0, N_PAGES - 1);
    b->scroll = 0;
    b->said[0] = 0;
    b->hover = -1;
    if (b->page < (int)N_LESSONS)
        b->seen[b->page] = true;
}

static int hit_at(brady_t *b, int x, int y)
{
    for (int i = b->n_hits - 1; i >= 0; i--)
        if (b->hits[i].r.h > 0 && rect_contains(b->hits[i].r, x, y))
            return i;
    return -1;
}

static void pick_answer(brady_t *b, int i)
{
    const quiz_t *q = &quiz[b->order[b->q]];
    b->picked = i;
    b->answered++;
    static const char *const yay[] = { "Nailed it!", "Yes! Fox-tastic!", "Correct!", "You're a natural!" };
    if (i == q->right) {
        b->score++;
        ksnprintf(b->said, sizeof b->said, "%s %s", yay[krand() % 4], q->why);
    } else {
        ksnprintf(b->said, sizeof b->said, "Close! The answer is %s. %s", q->a[q->right], q->why);
    }
}

static void br_mouse(window_t *w, int kind, int x, int y, int button)
{
    brady_t *b = w->data;
    if (kind == MS_WHEEL) {
        rect_t c = wm_client_rect(w);
        int view_h = c.h - 20 - BTN_H - 28;
        b->scroll = CLAMP(b->scroll - button * 40, 0, MAX(0, b->content_h - view_h));
        wm_invalidate_client(w);
        return;
    }
    int h = hit_at(b, x, y);
    if (kind == MS_MOVE || kind == MS_LEAVE) {
        if (kind == MS_LEAVE)
            h = -1;
        if (h != b->hover) {
            b->hover = h;
            ui_set_cursor(h >= 0 && b->hits[h].kind != HIT_FIELD ? CURSOR_HAND : CURSOR_ARROW);
            wm_invalidate_client(w);
        }
        return;
    }
    if (kind != MS_DOWN || h < 0)
        return;
    hit_t t = b->hits[h];
    switch (t.kind) {
    case HIT_PAGE: go(b, t.arg); break;
    case HIT_BACK: go(b, b->page - 1); break;
    case HIT_NEXT: go(b, b->page + 1); break;
    case HIT_CMD: open_terminal(b, t.cmd); break;
    case HIT_TERMINAL: open_terminal(b, NULL); break;
    case HIT_ANSWER: pick_answer(b, t.arg); break;
    case HIT_QUIZ_NEXT:
        if (b->q + 1 < (int)N_QUIZ) {
            b->q++;
            b->picked = -1;
            b->said[0] = 0;
        } else {
            ksnprintf(b->said, sizeof b->said, "That's all of them! You got %d out of %d. Let's go again!", b->score,
                      b->answered);
            shuffle_quiz(b);
            b->score = b->answered = 0;
        }
        break;
    }
    b->hover = -1;
    wm_invalidate_client(w);
}

static void br_key(window_t *w, const input_event_t *ev)
{
    brady_t *b = w->data;
    if (!ev->pressed)
        return;
    if (b->page == PAGE_FINDER && textfield_key(&b->search, ev)) {
        b->scroll = 0;
        wm_invalidate_client(w);
        return;
    }
    if (b->page == PAGE_QUIZ && b->picked < 0 && ev->key >= '1' && ev->key <= '4') {
        pick_answer(b, ev->key - '1');
    } else if (ev->key == KEY_DOWN || ev->key == KEY_PGDN) {
        b->scroll += ev->key == KEY_DOWN ? 40 : 300;
        rect_t c = wm_client_rect(w);
        b->scroll = CLAMP(b->scroll, 0, MAX(0, b->content_h - (c.h - 20 - BTN_H - 28)));
    } else if (ev->key == KEY_UP || ev->key == KEY_PGUP) {
        b->scroll = MAX(0, b->scroll - (ev->key == KEY_UP ? 40 : 300));
    } else if (ev->key == KEY_RIGHT) {
        go(b, b->page + 1);
    } else if (ev->key == KEY_LEFT) {
        go(b, b->page - 1);
    } else {
        return;
    }
    wm_invalidate_client(w);
}

static void br_tick(window_t *w)
{
    brady_t *b = w->data;
    if (b->page == PAGE_FINDER && wm_active() == w)
        wm_invalidate_client(w); /* the text cursor blinks */
}

static void br_close(window_t *w) { kfree(w->data); }

static const window_ops_t br_ops = { br_paint, br_key, br_mouse, br_tick, br_close, NULL };

window_t *brady_open(void)
{
    window_t *w = wm_find(&br_ops);
    if (w) {
        wm_focus(w);
        return w;
    }
    brady_t *b = kzalloc(sizeof *b);
    b->hover = -1;
    shuffle_quiz(b);
    go(b, 0);
    rect_t a = ui_work_area();
    int cw = MIN(980, a.w - 40), ch = MIN(640, a.h - TITLE_H - 20);
    w = wm_create("BradyBuddy", ICON_BRADY, cw, ch, &br_ops, b, WF_NO_MAX);
    wm_set_app(w, "BradyBuddy", NULL, NULL, NULL);
    wm_set_tick(w, 530);
    wm_center(w);
    return w;
}
