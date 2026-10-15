#pragma once

#include "kernel.h"

/* An in-memory file system. Paths are absolute, '/'-separated and
 * case-insensitive; the shell shows the root as drive C:. */

typedef struct fnode {
    char name[64];
    bool dir;
    uint8_t *data;
    size_t size, cap;
    bool external; /* data points into the kernel image (read-only) */
    datetime_t mtime;
    struct fnode *parent, *child, *next;
} fnode_t;

void fs_init(void);
fnode_t *fs_root(void);
fnode_t *fs_lookup(const char *path);
fnode_t *fs_lookup_in(fnode_t *dir, const char *name);
fnode_t *fs_create(const char *path, bool dir);
fnode_t *fs_create_in(fnode_t *dir, const char *name, bool dir_flag);
bool fs_write(fnode_t *f, const void *data, size_t n);
bool fs_append(fnode_t *f, const void *data, size_t n);
void fs_remove(fnode_t *f);
bool fs_rename(fnode_t *f, const char *name);
bool fs_move(fnode_t *f, fnode_t *dir);
void fs_path(const fnode_t *f, char *buf, size_t n);
int fs_count(const fnode_t *dir);
const char *fs_ext(const char *name);
size_t fs_used_bytes(void);
void fs_unique_name(fnode_t *dir, const char *base, const char *ext, char *out, size_t n);
void fs_resolve(const char *cwd, const char *rel, char *out, size_t n);
