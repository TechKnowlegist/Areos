// ramfs.c - the in-memory file system (see ramfs.h).

#include "desktop.h"
#include "ramfs.h"

void *malloc(size_t size);
void free(void *p);
void *memcpy(void *to, const void *from, size_t n);

struct node {
    bool used;
    bool is_dir;
    int parent;
    uint32_t uid;
    char name[RAMFS_NAME_MAX];
    char *data;
    int size;
};

static struct node nodes[RAMFS_MAX_NODES];
static uint32_t next_uid = 1;
static int total_bytes = 0;

int ramfs_root(void) { return 0; }

bool ramfs_valid(int id) { return id >= 0 && id < RAMFS_MAX_NODES && nodes[id].used; }
bool ramfs_is_dir(int id) { return ramfs_valid(id) && nodes[id].is_dir; }
const char *ramfs_name(int id) { return ramfs_valid(id) ? nodes[id].name : ""; }
int ramfs_parent(int id) { return ramfs_valid(id) ? nodes[id].parent : -1; }
uint32_t ramfs_uid(int id) { return ramfs_valid(id) ? nodes[id].uid : 0; }

static bool same_name(const char *a, const char *b) {
    for (;; a++, b++) {
        if (to_lower(*a) != to_lower(*b)) return false;
        if (*a == '\0') return true;
    }
}

int ramfs_find(int dir, const char *name) {
    if (!ramfs_is_dir(dir)) return -1;
    for (int i = 0; i < RAMFS_MAX_NODES; i++) {
        if (nodes[i].used && i != 0 && nodes[i].parent == dir && same_name(nodes[i].name, name)) return i;
    }
    return -1;
}

bool ramfs_name_ok(const char *name) {
    int n = text_length(name);
    if (n == 0 || n >= RAMFS_NAME_MAX) return false;
    if (text_equal(name, ".") || text_equal(name, "..")) return false;
    bool all_space = true;
    for (int i = 0; i < n; i++) {
        char c = name[i];
        if (c < 32 || c > 126 || c == '/' || c == '\\') return false;
        if (c != ' ') all_space = false;
    }
    return !all_space && name[0] != ' ' && name[n - 1] != ' ';
}

static int new_node(int dir, const char *name, bool is_dir) {
    for (int i = 1; i < RAMFS_MAX_NODES; i++) {
        if (!nodes[i].used) {
            struct node *n = &nodes[i];
            n->used = true; n->is_dir = is_dir; n->parent = dir; n->uid = next_uid++;
            text_copy(n->name, name, RAMFS_NAME_MAX);
            n->data = NULL; n->size = 0;
            return i;
        }
    }
    return RAMFS_FULL;
}

int ramfs_create(int dir, const char *name, bool is_dir) {
    if (!ramfs_is_dir(dir)) return RAMFS_NOT_DIR;
    if (!ramfs_name_ok(name)) return RAMFS_BAD_NAME;
    if (ramfs_find(dir, name) >= 0) return RAMFS_EXISTS;
    return new_node(dir, name, is_dir);
}

int ramfs_child_count(int dir) {
    int n = 0;
    for (int i = 1; i < RAMFS_MAX_NODES; i++) if (nodes[i].used && nodes[i].parent == dir) n++;
    return n;
}

static int compare(int a, int b) {
    if (nodes[a].is_dir != nodes[b].is_dir) return nodes[a].is_dir ? -1 : 1;
    const char *x = nodes[a].name, *y = nodes[b].name;
    for (;; x++, y++) {
        char p = to_lower(*x), q = to_lower(*y);
        if (p != q) return p < q ? -1 : 1;
        if (*x == '\0') return 0;
    }
}

int ramfs_list(int dir, int *ids, int max) {
    int count = 0;
    for (int i = 1; i < RAMFS_MAX_NODES && count < max; i++) {
        if (!nodes[i].used || nodes[i].parent != dir) continue;
        int k = count++;
        while (k > 0 && compare(ids[k - 1], i) > 0) { ids[k] = ids[k - 1]; k--; }
        ids[k] = i;
    }
    return count;
}

static void drop_data(struct node *n) {
    if (n->data) { free(n->data); total_bytes -= n->size; }
    n->data = NULL; n->size = 0;
}

static void remove_node(int id) {
    if (nodes[id].is_dir) {
        for (int i = 1; i < RAMFS_MAX_NODES; i++) if (nodes[i].used && nodes[i].parent == id) remove_node(i);
    }
    drop_data(&nodes[id]);
    nodes[id].used = false;
}

int ramfs_remove(int id) {
    if (!ramfs_valid(id)) return RAMFS_NOT_FOUND;
    if (id == 0) return RAMFS_PROTECTED;
    remove_node(id);
    return RAMFS_OK;
}

int ramfs_rename(int id, const char *name) {
    if (!ramfs_valid(id)) return RAMFS_NOT_FOUND;
    if (id == 0) return RAMFS_PROTECTED;
    if (!ramfs_name_ok(name)) return RAMFS_BAD_NAME;
    int other = ramfs_find(nodes[id].parent, name);
    if (other >= 0 && other != id) return RAMFS_EXISTS;
    text_copy(nodes[id].name, name, RAMFS_NAME_MAX);
    return RAMFS_OK;
}

int ramfs_size(int id) {
    if (!ramfs_valid(id)) return 0;
    return nodes[id].is_dir ? ramfs_child_count(id) : nodes[id].size;
}

const char *ramfs_data(int id) {
    return ramfs_valid(id) && !nodes[id].is_dir && nodes[id].data ? nodes[id].data : "";
}

int ramfs_write(int id, const char *data, int length) {
    if (!ramfs_valid(id) || nodes[id].is_dir) return RAMFS_NOT_FOUND;
    if (length < 0) length = 0;
    if (length > RAMFS_MAX_FILE) return RAMFS_TOO_BIG;
    if (total_bytes - nodes[id].size + length > RAMFS_MAX_TOTAL) return RAMFS_FULL;
    char *copy = NULL;
    if (length > 0) {
        copy = malloc((size_t)length);
        if (!copy) return RAMFS_FULL;
        memcpy(copy, data, (size_t)length);
    }
    drop_data(&nodes[id]);
    nodes[id].data = copy;
    nodes[id].size = length;
    total_bytes += length;
    return RAMFS_OK;
}

bool ramfs_is_inside(int id, int ancestor) {
    for (int guard = 0; ramfs_valid(id) && guard < RAMFS_MAX_NODES; guard++) {
        if (id == ancestor) return true;
        id = nodes[id].parent;
    }
    return false;
}

void ramfs_path(int id, char *out, int max) {
    int chain[32], depth = 0;
    for (int guard = 0; ramfs_valid(id) && id != 0 && depth < 32 && guard < 64; guard++) {
        chain[depth++] = id;
        id = nodes[id].parent;
    }
    out[0] = '\0';
    if (depth == 0) { text_copy(out, "/", max); return; }
    for (int i = depth - 1; i >= 0; i--) {
        text_append(out, "/", max);
        text_append(out, nodes[chain[i]].name, max);
    }
}

void ramfs_unique_name(int dir, const char *base, const char *ext, char *out, int max) {
    for (int n = 1; n < 1000; n++) {
        text_copy(out, base, max);
        if (n > 1) { text_append(out, " ", max); text_append_number(out, n, max); }
        text_append(out, ext, max);
        if (ramfs_find(dir, out) < 0) return;
    }
}

const char *ramfs_error_words(int error) {
    switch (error) {
    case RAMFS_EXISTS: return "Something with that name is already here.";
    case RAMFS_FULL: return "There is no room left.";
    case RAMFS_BAD_NAME: return "That isn't a good name. Use letters, numbers and spaces, without slashes.";
    case RAMFS_NOT_FOUND: return "That isn't there any more.";
    case RAMFS_NOT_DIR: return "That isn't a folder.";
    case RAMFS_TOO_BIG: return "That file is too big.";
    case RAMFS_PROTECTED: return "The top folder can't be changed.";
    case RAMFS_INSIDE: return "A folder can't go inside itself.";
    }
    return "Done.";
}

void ramfs_init(void) {
    static bool done = false;
    if (done) return;
    done = true;
    for (int i = 0; i < RAMFS_MAX_NODES; i++) { drop_data(&nodes[i]); nodes[i].used = false; }
    total_bytes = 0;
    nodes[0].used = true; nodes[0].is_dir = true; nodes[0].parent = -1; nodes[0].uid = next_uid++;
    text_copy(nodes[0].name, "Files", RAMFS_NAME_MAX);
    ramfs_create(0, "Documents", true);
    int w = ramfs_create(0, "Welcome.txt", false);
    if (w > 0) {
        static const char msg[] =
            "Welcome to AREOS Files.\n"
            "\n"
            "You can make folders and documents here, and write in them with Notes.\n"
            "Everything is kept in memory for now: it stays while AREOS is on and is gone after a restart.\n";
        ramfs_write(w, msg, (int)sizeof(msg) - 1);
    }
}
