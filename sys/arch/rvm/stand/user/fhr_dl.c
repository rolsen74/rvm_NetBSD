/* Native FHR shared-library loader for the static RVM userland.
 *
 * The first ABI uses the FHR type-2 image produced by fhr_so.py.  Its
 * COD1/DAT1/BSS1 sections carry REL0 local relocations, definitions and
 * imports.  A LIB0 record names libraries to load before resolving imports.
 */
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define FHR_MAX_FILE (16 * 1024 * 1024)
#define FHR_MAX_SECTIONS 32
#define FHR_MAX_RECORDS 65536

struct cursor {
    const unsigned char *p, *end;
};
struct section {
    unsigned char *base;
    size_t size, mapped;
    unsigned flags;
    unsigned init_kind;
};
#define FHR_CTORS 1
#define FHR_DTORS 2
#define FHR_INIT_ARRAY 3
#define FHR_FINI_ARRAY 4
struct symbol {
    const char *name;
    unsigned section;
    size_t offset;
};
struct relocation {
    unsigned source, target, type;
    size_t offset;
    const char *name;
};
struct library {
    struct library *next;
    char *path;
    unsigned char *file;
    size_t file_size;
    unsigned section_count, name_count;
    struct section sections[FHR_MAX_SECTIONS];
    const char **names;
    struct symbol *exports;
    size_t export_count;
    struct relocation *relocs;
    size_t reloc_count;
    unsigned users, opens;
    int global, closing, finalized;
    struct library *dependencies[FHR_MAX_SECTIONS];
    unsigned dependency_count;
    const char *needed_names[FHR_MAX_SECTIONS];
    struct library *needed[FHR_MAX_SECTIONS];
    unsigned needed_name_count, needed_count;
};

static struct library *libraries;
struct loading_path {
    struct loading_path *next;
    const char *path;
};
static struct loading_path *loading_paths;
static char error_text[128];
static int error_pending;
static unsigned long error_generation;

static void
set_error(const char *message)
{
    size_t length = strlen(message);
    if (length >= sizeof(error_text))
        length = sizeof(error_text) - 1;
    memcpy(error_text, message, length);
    error_text[length] = 0;
    error_pending = 1;
    error_generation++;
}

static int
take(struct cursor *c, size_t n, const unsigned char **p)
{
    if (n > (size_t)(c->end - c->p))
        return 0;
    *p = c->p;
    c->p += n;
    return 1;
}

static uint16_t
be16(const unsigned char *p)
{
    return ((uint16_t)p[0] << 8) | p[1];
}

static uint32_t
be32(const unsigned char *p)
{
    return ((uint32_t)be16(p) << 16) | be16(p + 2);
}

static uint64_t
be64(const unsigned char *p)
{
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static int
unpack_image(struct library *lib)
{
    unsigned char *expanded, *old = lib->file;
    uint64_t packed, total;
    uLongf output_size;
    uLong input_size;

    if (lib->file_size < 16 || old[7] == 0)
        return 1;
    if (old[7] != 1 || old[4] != 1 || old[5] != 1 ||
        lib->file_size < 32)
        return 0;
    packed = be64(old + 16);
    total = be64(old + 24);
    if (packed != lib->file_size - 32 || total < 16 ||
        total > FHR_MAX_FILE)
        return 0;
    expanded = malloc((size_t)total);
    if (!expanded)
        return 0;
    memcpy(expanded, old, 16);
    expanded[7] = 0;
    output_size = (uLongf)(total - 16);
    input_size = (uLong)packed;
    if (uncompress2(expanded + 16, &output_size, old + 32,
                    &input_size) != Z_OK || output_size != total - 16 ||
        input_size != packed) {
        free(expanded);
        return 0;
    }
    free(old);
    lib->file = expanded;
    lib->file_size = (size_t)total;
    return 1;
}

static void
put64(unsigned char *p, uint64_t value)
{
    for (unsigned i = 0; i < 8; i++)
        p[i] = (unsigned char)(value >> (56 - 8 * i));
}

static int
integer(struct cursor *c, uint64_t *value)
{
    const unsigned char *p;
    unsigned mask;
    if (!take(c, 1, &p))
        return 0;
    mask = *p;
    *value = 0;
    if (mask < 0x80) {
        *value = mask;
        return 1;
    }
    for (unsigned bit = 0; bit < 8; bit++) {
        *value <<= 8;
        if (mask & (1U << bit)) {
            if (!take(c, 1, &p))
                return 0;
            *value |= *p;
        }
    }
    return 1;
}

static const char *
name_at(struct library *lib, uint64_t index)
{
    if (!index || index > lib->name_count)
        return NULL;
    return lib->names[index];
}

static int
append_export(struct library *lib, unsigned section, uint64_t offset,
    const char *name)
{
    struct symbol *list;
    if (!name || offset > lib->sections[section].size ||
        lib->export_count == FHR_MAX_RECORDS)
        return 0;
    list = realloc(lib->exports, (lib->export_count + 1) * sizeof(*list));
    if (!list)
        return 0;
    lib->exports = list;
    lib->exports[lib->export_count++] =
        (struct symbol){name, section, (size_t)offset};
    return 1;
}

static int
append_relocation(struct library *lib, unsigned source, unsigned target,
    unsigned type, uint64_t offset, const char *name)
{
    struct relocation *list;
    if (offset > lib->sections[source].size ||
        lib->sections[source].size - offset < 8 ||
        lib->reloc_count == FHR_MAX_RECORDS || (type == 2 && !name))
        return 0;
    list = realloc(lib->relocs, (lib->reloc_count + 1) * sizeof(*list));
    if (!list)
        return 0;
    lib->relocs = list;
    lib->relocs[lib->reloc_count++] =
        (struct relocation){source, target, type, (size_t)offset, name};
    return 1;
}

static int
parse_names(struct library *lib, struct cursor chunk)
{
    uint64_t count, length, term;
    const unsigned char *p;
    if (!integer(&chunk, &count) || count > FHR_MAX_RECORDS)
        return 0;
    lib->name_count = (unsigned)count;
    lib->names = calloc(count + 1, sizeof(*lib->names));
    if (!lib->names)
        return 0;
    for (uint64_t i = 1; i <= count; i++) {
        if (!integer(&chunk, &length) || !length ||
            !take(&chunk, length, &p) || p[length - 1] != 0 ||
            memchr(p, 0, length - 1) != NULL)
            return 0;
        lib->names[i] = (const char *)p;
    }
    return integer(&chunk, &term) && term == 0 && chunk.p == chunk.end;
}

static int
parse_needed(struct library *lib, struct cursor chunk)
{
    uint64_t count, index, flags;
    if (!integer(&chunk, &count) || count == 0 ||
        count > FHR_MAX_SECTIONS)
        return 0;
    for (uint64_t item = 0; item < count; item++) {
        const char *name;
        size_t length;
        if (!integer(&chunk, &index) || !integer(&chunk, &flags) || flags ||
            (name = name_at(lib, index)) == NULL)
            return 0;
        length = strlen(name);
        if (!length || length > 127 || strcmp(name, ".") == 0 ||
            strcmp(name, "..") == 0)
            return 0;
        for (size_t i = 0; i < length; i++)
            if ((unsigned char)name[i] < 0x21 ||
                (unsigned char)name[i] > 0x7e ||
                name[i] == '/' || name[i] == '\\')
                return 0;
        for (unsigned i = 0; i < lib->needed_name_count; i++)
            if (strcmp(lib->needed_names[i], name) == 0)
                return 0;
        lib->needed_names[lib->needed_name_count++] = name;
    }
    return chunk.p == chunk.end;
}

static int
parse_relocations(struct library *lib, unsigned source, struct cursor chunk)
{
    uint64_t count, target, type, offset, name_index;
    while (chunk.p != chunk.end) {
        if (!integer(&chunk, &count))
            return 0;
        if (!count)
            return chunk.p == chunk.end;
        if (count > FHR_MAX_RECORDS || !integer(&chunk, &target) ||
            target >= lib->section_count || !integer(&chunk, &type) || type > 2)
            return 0;
        for (uint64_t i = 0; i < count; i++) {
            if (!integer(&chunk, &offset))
                return 0;
            if (type == 0) {
                if (!append_relocation(lib, source, (unsigned)target, 0,
                                       offset, NULL))
                    return 0;
            } else {
                if (!integer(&chunk, &name_index))
                    return 0;
                if (type == 1) {
                    if (!append_export(lib, (unsigned)target, offset,
                                       name_at(lib, name_index)))
                        return 0;
                } else if (!append_relocation(lib, source, (unsigned)target,
                                              2, offset,
                                              name_at(lib, name_index)))
                    return 0;
            }
        }
    }
    return 0;
}

static int
parse_image(struct library *lib)
{
    struct cursor file = {lib->file, lib->file + lib->file_size}, chunk;
    const unsigned char *p, *section_info, *payload;
    uint64_t length, size;
    uint32_t tag;
    unsigned flags, align;
    long pagesize = sysconf(_SC_PAGESIZE);

    if (pagesize <= 0 || !take(&file, 16, &p) ||
        memcmp(p, "FHR\0\1\1\2\0\0\0\1\3\1\0", 14) != 0 ||
        (lib->section_count = be16(p + 14)) == 0 ||
        lib->section_count > FHR_MAX_SECTIONS ||
        !take(&file, 12 * lib->section_count, &section_info))
        return 0;
    for (unsigned i = 0; i < lib->section_count; i++) {
        p = section_info + 12 * i;
        size = be64(p);
        align = be16(p + 8);
        flags = be16(p + 10);
        if (!size || size > FHR_MAX_FILE || align > 16 ||
            (flags & ~7U) || !(flags & 6) ||
            ((flags & 5) == 5) ||
            size > SIZE_MAX - (size_t)pagesize + 1)
            return 0;
        struct section *s = &lib->sections[i];
        s->size = (size_t)size;
        s->mapped = (s->size + pagesize - 1) & ~(size_t)(pagesize - 1);
        s->flags = flags;
        s->base = mmap(NULL, s->mapped, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANON, -1, 0);
        if (s->base == MAP_FAILED) {
            s->base = NULL;
            return 0;
        }
    }
    if (!take(&file, 12, &p) || be32(p) != 0x53594d00)
        return 0;
    length = be64(p + 4);
    if (length > (uint64_t)(file.end - file.p) ||
        !take(&file, (size_t)length, &payload))
        return 0;
    chunk = (struct cursor){payload, payload + length};
    if (!parse_names(lib, chunk))
        return 0;

    if (file.end - file.p >= 4 && be32(file.p) == 0x4c494200) {
        if (!take(&file, 12, &p) ||
            (length = be64(p + 4)) > (uint64_t)(file.end - file.p) ||
            !take(&file, (size_t)length, &payload) ||
            !parse_needed(lib, (struct cursor){payload, payload + length}))
            return 0;
    }

    for (unsigned i = 0; i < lib->section_count; i++) {
        if (!take(&file, 12, &p))
            return 0;
        tag = be32(p);
        length = be64(p + 4);
        if (length > (uint64_t)(file.end - file.p) ||
            !take(&file, (size_t)length, &payload))
            return 0;
        struct section *s = &lib->sections[i];
        unsigned name_index;
        if (tag == 0x42535301) {
            if (length != 14 || be64(payload) != s->size ||
                be16(payload + 8) != be16(section_info + 12 * i + 8) ||
                be16(payload + 10) != s->flags)
                return 0;
            name_index = be16(payload + 12);
        } else {
            if (length != s->size + 6 ||
                tag != ((s->flags & 4) ? 0x434f4401 : 0x44415401) ||
                be16(payload) != be16(section_info + 12 * i + 8) ||
                be16(payload + 2) != s->flags)
                return 0;
            name_index = be16(payload + 4);
            memcpy(s->base, payload + 6, s->size);
        }
        const char *name = name_at(lib, name_index);
        if (name_index && !name)
            return 0;
        if (name) {
            if (strcmp(name, ".ctors") == 0 ||
                strncmp(name, ".ctors.", 7) == 0)
                s->init_kind = FHR_CTORS;
            else if (strcmp(name, ".dtors") == 0 ||
                     strncmp(name, ".dtors.", 7) == 0)
                s->init_kind = FHR_DTORS;
            else if (strcmp(name, ".init_array") == 0 ||
                     strncmp(name, ".init_array.", 12) == 0)
                s->init_kind = FHR_INIT_ARRAY;
            else if (strcmp(name, ".fini_array") == 0 ||
                     strncmp(name, ".fini_array.", 12) == 0)
                s->init_kind = FHR_FINI_ARRAY;
        }
        if (s->init_kind && (tag == 0x42535301 || s->size % 8 != 0 ||
                             (s->flags & 4)))
            return 0;
        for (;;) {
            if (!take(&file, 4, &p))
                return 0;
            tag = be32(p);
            if (tag == 0x454e4400)
                break;
            if (tag != 0x52454c00 || !take(&file, 8, &p))
                return 0;
            length = be64(p);
            if (length > (uint64_t)(file.end - file.p) ||
                !take(&file, (size_t)length, &payload))
                return 0;
            chunk = (struct cursor){payload, payload + length};
            if (!parse_relocations(lib, i, chunk))
                return 0;
        }
    }
    return file.p == file.end;
}

static void *
find_symbol(struct library *lib, const char *name)
{
    for (size_t i = 0; i < lib->export_count; i++)
        if (strcmp(lib->exports[i].name, name) == 0)
            return lib->sections[lib->exports[i].section].base +
                lib->exports[i].offset;
    return NULL;
}

static void *
find_dependency_symbol(struct library *lib, const char *name,
    struct library **provider, unsigned depth)
{
    if (depth >= FHR_MAX_SECTIONS)
        return NULL;
    for (unsigned i = 0; i < lib->dependency_count; i++) {
        struct library *dependency = lib->dependencies[i];
        void *address = find_symbol(dependency, name);
        if (!address)
            address = find_dependency_symbol(dependency, name, provider,
                                             depth + 1);
        else
            *provider = dependency;
        if (address)
            return address;
    }
    return NULL;
}

static int
add_dependency(struct library *consumer, struct library *provider)
{
    for (unsigned i = 0; i < consumer->dependency_count; i++)
        if (consumer->dependencies[i] == provider)
            return 1;
    if (consumer->dependency_count == FHR_MAX_SECTIONS)
        return 0;
    consumer->dependencies[consumer->dependency_count++] = provider;
    provider->users++;
    return 1;
}

static int
load_dependencies(struct library *lib)
{
    const char *slash = strrchr(lib->path, '/');
    size_t prefix = slash ? (size_t)(slash - lib->path + 1) : 0;
    for (unsigned i = 0; i < lib->needed_name_count; i++) {
        const char *name = lib->needed_names[i];
        size_t length = strlen(name);
        struct library *dependency;
        char *path;
        if (prefix > SIZE_MAX - length - 1)
            return 0;
        path = malloc(prefix + length + 1);
        if (!path)
            return 0;
        memcpy(path, lib->path, prefix);
        memcpy(path + prefix, name, length + 1);
        dependency = dlopen(path, RTLD_NOW);
        free(path);
        if (!dependency)
            return 0;
        if (!add_dependency(lib, dependency)) {
            dlclose(dependency);
            return 0;
        }
        lib->needed[lib->needed_count++] = dependency;
    }
    return 1;
}

static int
apply_relocations(struct library *lib)
{
    for (size_t i = 0; i < lib->reloc_count; i++) {
        struct relocation *r = &lib->relocs[i];
        unsigned char *site = lib->sections[r->source].base + r->offset;
        uint64_t addend = be64(site), address;
        if (r->type == 0)
            address = (uintptr_t)lib->sections[r->target].base;
        else {
            struct library *provider = NULL;
            void *resolved = find_symbol(lib, r->name);
            if (!resolved)
                resolved = find_dependency_symbol(lib, r->name, &provider, 0);
            if (provider && !add_dependency(lib, provider))
                return 0;
            if (!resolved)
                for (struct library *other = libraries; other; other = other->next)
                    if (other->global &&
                        (resolved = find_symbol(other, r->name)) != NULL) {
                        if (!add_dependency(lib, other))
                            return 0;
                        break;
                    }
            if (!resolved) {
                set_error("FHR shared library has an unresolved import");
                return 0;
            }
            address = (uintptr_t)resolved;
        }
        if (addend > UINT64_MAX - address)
            return 0;
        put64(site, address + addend);
    }
    for (unsigned i = 0; i < lib->section_count; i++) {
        struct section *s = &lib->sections[i];
        int prot = PROT_READ | ((s->flags & 1) ? PROT_WRITE : 0) |
            ((s->flags & 4) ? PROT_EXEC : 0);
        if (mprotect(s->base, s->mapped, prot) != 0)
            return 0;
    }
    return 1;
}

static void
run_array(struct section *section, int reverse)
{
    size_t count = section->size / 8;
    for (size_t i = 0; i < count; i++) {
        size_t slot = reverse ? count - 1 - i : i;
        uint64_t address = be64(section->base + slot * 8);
        if (address && address != UINT64_MAX)
            ((void (*)(void))(uintptr_t)address)();
    }
}

static void
run_initializers(struct library *lib)
{
    for (unsigned i = 0; i < lib->section_count; i++) {
        struct section *section = &lib->sections[i];
        if (section->init_kind == FHR_CTORS)
            run_array(section, 1);
        else if (section->init_kind == FHR_INIT_ARRAY)
            run_array(section, 0);
    }
}

static void
run_finalizers(struct library *lib)
{
    if (lib->finalized)
        return;
    lib->finalized = 1;
    for (unsigned i = lib->section_count; i != 0; i--) {
        struct section *section = &lib->sections[i - 1];
        if (section->init_kind == FHR_DTORS)
            run_array(section, 0);
        else if (section->init_kind == FHR_FINI_ARRAY)
            run_array(section, 1);
    }
}

static void
close_at_exit(void)
{
    for (struct library *lib = libraries; lib; lib = lib->next) {
        lib->closing = 1;
        run_finalizers(lib);
    }
}

static void
free_library(struct library *lib)
{
    if (!lib)
        return;
    for (unsigned i = 0; i < lib->dependency_count; i++)
        lib->dependencies[i]->users--;
    for (unsigned i = 0; i < lib->needed_count; i++)
        dlclose(lib->needed[i]);
    for (unsigned i = 0; i < lib->section_count; i++)
        if (lib->sections[i].base)
            munmap(lib->sections[i].base, lib->sections[i].mapped);
    free(lib->relocs);
    free(lib->exports);
    free(lib->names);
    free(lib->file);
    free(lib->path);
    free(lib);
}

void *
dlopen(const char *path, int mode)
{
    struct stat st;
    struct library *lib;
    struct loading_path active;
    int fd;
    size_t done = 0;
    unsigned loading_depth = 0;
    unsigned long previous_error = error_generation;
    if (!path || !*path) {
        set_error("FHR dlopen requires a library path");
        return NULL;
    }
    for (lib = libraries; lib; lib = lib->next)
        if (strcmp(lib->path, path) == 0) {
            if (lib->closing) {
                set_error("FHR shared library is closing");
                return NULL;
            }
            lib->opens++;
            if (mode & RTLD_GLOBAL)
                lib->global = 1;
            return lib;
        }
    for (struct loading_path *item = loading_paths; item; item = item->next) {
        if (strcmp(item->path, path) == 0) {
            set_error("cyclic FHR shared-library dependency");
            return NULL;
        }
        loading_depth++;
    }
    if (loading_depth == FHR_MAX_SECTIONS) {
        set_error("FHR dependency chain is too deep");
        return NULL;
    }
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        set_error("cannot open FHR shared library");
        return NULL;
    }
    if (fstat(fd, &st) != 0 || st.st_size < 16 ||
        st.st_size > FHR_MAX_FILE) {
        close(fd);
        set_error("invalid FHR shared library size");
        return NULL;
    }
    lib = calloc(1, sizeof(*lib));
    if (!lib) {
        close(fd);
        set_error("out of memory loading FHR library");
        return NULL;
    }
    lib->path = strdup(path);
    lib->file_size = (size_t)st.st_size;
    lib->file = malloc(lib->file_size);
    if (!lib->path || !lib->file) {
        close(fd);
        free_library(lib);
        set_error("out of memory loading FHR library");
        return NULL;
    }
    while (done < lib->file_size) {
        ssize_t n = read(fd, lib->file + done, lib->file_size - done);
        if (n <= 0)
            break;
        done += (size_t)n;
    }
    close(fd);
    active = (struct loading_path){loading_paths, lib->path};
    loading_paths = &active;
    if (done != lib->file_size || !unpack_image(lib) ||
        !parse_image(lib) || !load_dependencies(lib) ||
        !apply_relocations(lib)) {
        loading_paths = active.next;
        free_library(lib);
        if (error_generation == previous_error)
            set_error("invalid or unsupported FHR shared library");
        return NULL;
    }
    loading_paths = active.next;
    lib->opens = 1;
    lib->global = !!(mode & RTLD_GLOBAL);
    lib->next = libraries;
    libraries = lib;
    static int exit_registered;
    if (!exit_registered && atexit(close_at_exit) == 0)
        exit_registered = 1;
    run_initializers(lib);
    return lib;
}

void *
dlsym(void *handle, const char *name)
{
    if (!handle || !name) {
        set_error("invalid FHR dlsym request");
        return NULL;
    }
    if (handle == RTLD_DEFAULT) {
        for (struct library *lib = libraries; lib; lib = lib->next) {
            void *address;
            if (lib->global && (address = find_symbol(lib, name)) != NULL)
                return address;
        }
        set_error("FHR shared library symbol not found");
        return NULL;
    }
    for (struct library *lib = libraries; lib; lib = lib->next)
        if (lib == handle) {
            void *address = find_symbol(lib, name);
            struct library *provider = NULL;
            if (!address)
                address = find_dependency_symbol(lib, name, &provider, 0);
            if (!address)
                set_error("FHR shared library symbol not found");
            return address;
        }
    set_error("invalid FHR shared library handle");
    return NULL;
}

int
dlclose(void *handle)
{
    struct library **link = &libraries;
    while (*link && *link != handle)
        link = &(*link)->next;
    if (!handle || !*link) {
        set_error("invalid FHR shared library handle");
        return -1;
    }
    struct library *lib = *link;
    if (lib->closing) {
        set_error("FHR shared library is closing");
        return -1;
    }
    if (lib->opens > 1) {
        lib->opens--;
        return 0;
    }
    if (lib->users) {
        set_error("FHR shared library is in use");
        return -1;
    }
    lib->closing = 1;
    run_finalizers(lib);
    link = &libraries;
    while (*link && *link != lib)
        link = &(*link)->next;
    if (!*link)
        return -1;
    *link = lib->next;
    free_library(lib);
    return 0;
}

char *
dlerror(void)
{
    if (!error_pending)
        return NULL;
    error_pending = 0;
    return error_text;
}
