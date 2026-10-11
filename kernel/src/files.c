// files.c - the files AREOS was booted with.

#include "files.h"
#include "text.h"

#define MAX_FILES 64

static struct file files[MAX_FILES];
static size_t count = 0;

// "/apps/hello.arx" -> "hello.arx"
static const char *base_name(const char *path) {
    const char *name = path;
    for (const char *c = path; *c != '\0'; c++) {
        if (*c == '/') {
            name = c + 1;
        }
    }
    return name;
}

void files_init(struct limine_module_response *modules) {
    if (modules == NULL) {
        return;
    }
    for (uint64_t i = 0; i < modules->module_count && count < MAX_FILES; i++) {
        struct limine_file *module = modules->modules[i];
        files[count].name = base_name(module->path);
        files[count].data = module->address;
        files[count].size = module->size;
        count++;
    }
}

size_t files_count(void) {
    return count;
}

const struct file *files_get(size_t index) {
    return index < count ? &files[index] : NULL;
}

// True if `name` matches `file_name`, either exactly or without ".arx".
static bool name_matches(const char *name, const char *file_name) {
    if (text_equal_ignore_case(name, file_name)) {
        return true;
    }
    size_t length = text_length(name);
    size_t file_length = text_length(file_name);
    if (file_length != length + 4) {
        return false;
    }
    if (!text_equal_ignore_case(file_name + length, ".arx")) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        char a = name[i], b = file_name[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) {
            return false;
        }
    }
    return true;
}

const struct file *files_find(const char *name) {
    for (size_t i = 0; i < count; i++) {
        if (name_matches(name, files[i].name)) {
            return &files[i];
        }
    }
    return NULL;
}
