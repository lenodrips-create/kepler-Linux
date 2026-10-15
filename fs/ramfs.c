/* RAM file system with a few starter documents. */

#include "ramfs.h"

static fnode_t root = { .name = "", .dir = true };

fnode_t *fs_root(void) { return &root; }

static void touch(fnode_t *f) { rtc_read(&f->mtime); }

fnode_t *fs_lookup_in(fnode_t *dir, const char *name)
{
    if (!dir || !dir->dir)
        return NULL;
    for (fnode_t *c = dir->child; c; c = c->next)
        if (strcasecmp(c->name, name) == 0)
            return c;
    return NULL;
}

fnode_t *fs_lookup(const char *path)
{
    fnode_t *cur = &root;
    char part[64];
    while (*path) {
        while (*path == '/')
            path++;
        if (!*path)
            break;
        int n = 0;
        while (*path && *path != '/' && n < 63)
            part[n++] = *path++;
        part[n] = 0;
        if (strcmp(part, ".") == 0)
            continue;
        if (strcmp(part, "..") == 0) {
            if (cur->parent)
                cur = cur->parent;
            continue;
        }
        cur = fs_lookup_in(cur, part);
        if (!cur)
            return NULL;
    }
    return cur;
}

/* children are kept sorted: folders first, then by name */
static int order(const fnode_t *a, const fnode_t *b)
{
    if (a->dir != b->dir)
        return a->dir ? -1 : 1;
    return strcasecmp(a->name, b->name);
}

static void insert_child(fnode_t *dir, fnode_t *f)
{
    f->parent = dir;
    fnode_t **pp = &dir->child;
    while (*pp && order(*pp, f) < 0)
        pp = &(*pp)->next;
    f->next = *pp;
    *pp = f;
}

static void detach(fnode_t *f)
{
    if (!f->parent)
        return;
    fnode_t **pp = &f->parent->child;
    while (*pp && *pp != f)
        pp = &(*pp)->next;
    if (*pp)
        *pp = f->next;
    f->next = NULL;
    f->parent = NULL;
}

fnode_t *fs_create_in(fnode_t *dir, const char *name, bool dir_flag)
{
    if (!dir || !dir->dir || !name[0] || strchr(name, '/'))
        return NULL;
    fnode_t *f = fs_lookup_in(dir, name);
    if (f)
        return f->dir == dir_flag ? f : NULL;
    f = kzalloc(sizeof *f);
    strlcpy(f->name, name, sizeof f->name);
    f->dir = dir_flag;
    touch(f);
    insert_child(dir, f);
    return f;
}

fnode_t *fs_create(const char *path, bool dir_flag)
{
    char parent[256];
    strlcpy(parent, path, sizeof parent);
    char *slash = strrchr(parent, '/');
    if (!slash)
        return NULL;
    *slash = 0;
    fnode_t *d = fs_lookup(parent[0] ? parent : "/");
    return fs_create_in(d, slash + 1, dir_flag);
}

bool fs_write(fnode_t *f, const void *data, size_t n)
{
    if (!f || f->dir)
        return false;
    if (f->external) {
        f->data = NULL;
        f->cap = 0;
        f->external = false;
    }
    if (n > f->cap) {
        uint8_t *p = kmalloc(n + 64);
        if (!p)
            return false;
        kfree(f->data);
        f->data = p;
        f->cap = n + 64;
    }
    memcpy(f->data, data, n);
    f->size = n;
    touch(f);
    return true;
}

bool fs_append(fnode_t *f, const void *data, size_t n)
{
    if (!f || f->dir)
        return false;
    if (f->external) {
        uint8_t *copy = kmalloc(f->size + n + 64);
        memcpy(copy, f->data, f->size);
        f->data = copy;
        f->cap = f->size + n + 64;
        f->external = false;
    }
    if (f->size + n > f->cap) {
        size_t cap = (f->size + n) * 2 + 64;
        uint8_t *p = krealloc(f->data, cap);
        if (!p)
            return false;
        f->data = p;
        f->cap = cap;
    }
    memcpy(f->data + f->size, data, n);
    f->size += n;
    touch(f);
    return true;
}

static void free_node(fnode_t *f)
{
    while (f->child) {
        fnode_t *c = f->child;
        f->child = c->next;
        free_node(c);
    }
    if (!f->external)
        kfree(f->data);
    kfree(f);
}

void fs_remove(fnode_t *f)
{
    if (!f || f == &root)
        return;
    detach(f);
    free_node(f);
}

bool fs_rename(fnode_t *f, const char *name)
{
    if (!f || f == &root || !name[0] || strchr(name, '/'))
        return false;
    fnode_t *other = fs_lookup_in(f->parent, name);
    if (other && other != f)
        return false;
    fnode_t *dir = f->parent;
    detach(f);
    strlcpy(f->name, name, sizeof f->name);
    insert_child(dir, f);
    return true;
}

bool fs_move(fnode_t *f, fnode_t *dir)
{
    if (!f || !dir || !dir->dir || f == &root)
        return false;
    for (fnode_t *p = dir; p; p = p->parent)
        if (p == f)
            return false; /* into itself */
    char name[64];
    strlcpy(name, f->name, sizeof name);
    if (fs_lookup_in(dir, name)) {
        const char *ext = fs_ext(f->name);
        char base[64];
        strlcpy(base, f->name, sizeof base);
        if (*ext)
            base[strlen(base) - strlen(ext) - 1] = 0;
        fs_unique_name(dir, base, ext, name, sizeof name);
    }
    detach(f);
    strlcpy(f->name, name, sizeof f->name);
    insert_child(dir, f);
    return true;
}

void fs_path(const fnode_t *f, char *buf, size_t n)
{
    const fnode_t *chain[32];
    int depth = 0;
    for (const fnode_t *p = f; p && p != &root && depth < 32; p = p->parent)
        chain[depth++] = p;
    buf[0] = 0;
    if (!depth) {
        strlcpy(buf, "/", n);
        return;
    }
    while (depth--) {
        strlcat(buf, "/", n);
        strlcat(buf, chain[depth]->name, n);
    }
}

int fs_count(const fnode_t *dir)
{
    int n = 0;
    for (fnode_t *c = dir->child; c; c = c->next)
        n++;
    return n;
}

const char *fs_ext(const char *name)
{
    const char *dot = strrchr(name, '.');
    return dot && dot != name ? dot + 1 : "";
}

static size_t used(const fnode_t *f)
{
    size_t n = f->size;
    for (fnode_t *c = f->child; c; c = c->next)
        n += used(c);
    return n;
}

size_t fs_used_bytes(void) { return used(&root); }

void fs_unique_name(fnode_t *dir, const char *base, const char *ext, char *out, size_t n)
{
    for (int i = 1;; i++) {
        if (i == 1)
            ksnprintf(out, n, "%s%s%s", base, *ext ? "." : "", ext);
        else
            ksnprintf(out, n, "%s (%d)%s%s", base, i, *ext ? "." : "", ext);
        if (!fs_lookup_in(dir, out))
            return;
    }
}

/* join a working directory and a relative path, normalising . and .. */
void fs_resolve(const char *cwd, const char *rel, char *out, size_t n)
{
    char tmp[256];
    if (rel[0] == '/')
        strlcpy(tmp, rel, sizeof tmp);
    else
        ksnprintf(tmp, sizeof tmp, "%s/%s", cwd, rel);

    char *parts[32];
    int np = 0;
    char *p = tmp;
    while (*p) {
        while (*p == '/')
            *p++ = 0;
        if (!*p)
            break;
        char *start = p;
        while (*p && *p != '/')
            p++;
        if (*p)
            *p++ = 0;
        if (strcmp(start, ".") == 0)
            continue;
        if (strcmp(start, "..") == 0) {
            if (np)
                np--;
            continue;
        }
        if (np < 32)
            parts[np++] = start;
    }
    out[0] = 0;
    if (!np)
        strlcpy(out, "/", n);
    for (int i = 0; i < np; i++) {
        strlcat(out, "/", n);
        strlcat(out, parts[i], n);
    }
}

/* ---- starter content ------------------------------------------------------------- */

extern const uint8_t wallpaper_jpg[], wallpaper_jpg_end[];

static void text_file(const char *path, const char *text) { fs_write(fs_create(path, false), text, strlen(text)); }

void fs_init(void)
{
    touch(&root);
    fs_create("/Documents", true);
    fs_create("/Documents/My Pictures", true);
    fs_create("/Desktop", true);
    fs_create("/Kepler", true);
    fs_create("/Recycled", true);

    text_file("/Documents/Welcome.txt",
              "Welcome to KeplerOS!\n"
              "\n"
              "KeplerOS is a small hobby operating system written in C.\n"
              "Everything you see is drawn by the kernel itself: the\n"
              "window manager, the fonts, the icons and even the JPEG\n"
              "decoder that shows the wallpaper.\n"
              "\n"
              "Things to try:\n"
              "  - Double-click My Computer to see your system.\n"
              "  - Open the Command Prompt and type 'help'.\n"
              "  - Press the Windows key to open the Start menu.\n"
              "  - Ctrl+Alt+Del opens the Task Manager.\n"
              "  - Right-click the desktop to make a new text file.\n"
              "\n"
              "Have fun exploring!\n");
    text_file("/Documents/Ideas.txt",
              "KeplerOS ideas\n"
              "==============\n"
              "* A FAT32 driver so files survive a reboot\n"
              "* Sound Blaster 16 support\n"
              "* A network stack (RTL8139 + TCP/IP)\n"
              "* User mode and real processes\n"
              "* More games!\n");
    text_file("/Kepler/version.txt", KEPLER_NAME " " KEPLER_VERSION " (build " KEPLER_BUILD ")\n");
    text_file("/Kepler/system.ini",
              "[desktop]\n"
              "wallpaper=Kepler Earth\n"
              "resolution=1280x800x32\n"
              "\n"
              "[shell]\n"
              "theme=Luna Blue\n"
              "startmenu=classic-two-column\n");

    fnode_t *pic = fs_create("/Documents/My Pictures/Kepler Earth.jpg", false);
    pic->data = (uint8_t *)wallpaper_jpg;
    pic->size = wallpaper_jpg_end - wallpaper_jpg;
    pic->external = true;
}
