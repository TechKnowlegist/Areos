// ramfs.h - a small file system that lives in memory.
//
// Folders and files are kept while AREOS is on and are gone after a restart.
// The File Manager and Notes both use it. Every file or folder has a number
// (its id); numbers get reused after something is deleted, so anything that
// remembers one should also remember its uid (ramfs_uid), which never repeats.

#ifndef RAMFS_H
#define RAMFS_H

#include <stdbool.h>
#include <stdint.h>

#define RAMFS_NAME_MAX 40          // longest name, with room for the end
#define RAMFS_MAX_NODES 512
#define RAMFS_MAX_FILE 65536       // biggest file, in bytes
#define RAMFS_MAX_TOTAL (4 * 1024 * 1024)

enum {
    RAMFS_OK = 0,
    RAMFS_EXISTS = -1,             // that name is already used in this folder
    RAMFS_FULL = -2,               // out of room
    RAMFS_BAD_NAME = -3,
    RAMFS_NOT_FOUND = -4,
    RAMFS_NOT_DIR = -5,
    RAMFS_TOO_BIG = -6,
    RAMFS_PROTECTED = -7,          // the top folder can't be removed or renamed
    RAMFS_INSIDE = -8,
};

void ramfs_init(void);             // empty, then a Documents folder and a Welcome note
int ramfs_root(void);
bool ramfs_valid(int id);
bool ramfs_is_dir(int id);
const char *ramfs_name(int id);
int ramfs_parent(int id);          // -1 for the top
uint32_t ramfs_uid(int id);
int ramfs_find(int dir, const char *name);          // id, or -1 (not case sensitive)
bool ramfs_name_ok(const char *name);
int ramfs_create(int dir, const char *name, bool is_dir);   // the new id, or a negative RAMFS_ error
int ramfs_list(int dir, int *ids, int max);          // folders first, then by name; returns how many
int ramfs_child_count(int dir);
int ramfs_remove(int id);          // a folder goes with everything inside it
int ramfs_rename(int id, const char *name);
int ramfs_size(int id);            // bytes (files); items inside (folders)
const char *ramfs_data(int id);    // the bytes (not ended with a zero); valid until the next write
int ramfs_write(int id, const char *data, int length);
void ramfs_path(int id, char *out, int max);        // "/Documents/Notes.txt"
bool ramfs_is_inside(int id, int ancestor);         // id is ancestor, or below it
const char *ramfs_error_words(int error);           // for saying out loud
// A name that isn't used yet: "Untitled folder", "Untitled folder 2"... `ext` is "" or ".txt".
void ramfs_unique_name(int dir, const char *base, const char *ext, char *out, int max);

#endif
