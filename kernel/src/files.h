// files.h - the files AREOS was booted with.
//
// AREOS can't read disks yet. Instead, the bootloader loads the apps
// into memory at boot (Limine calls them "modules"), and this list
// remembers where each one is.

#ifndef AREOS_FILES_H
#define AREOS_FILES_H

#include <stddef.h>
#include <limine.h>

struct file {
    const char *name;   // like "hello.arx"
    const void *data;
    size_t size;
};

void files_init(struct limine_module_response *modules);

size_t files_count(void);
const struct file *files_get(size_t index);

// Find a file by name. "hello" also finds "hello.arx". NULL if none.
const struct file *files_find(const char *name);

#endif
