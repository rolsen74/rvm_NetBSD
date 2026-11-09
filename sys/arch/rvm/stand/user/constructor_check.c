/* Ordered static initialization and normal/recursive exit in real processes. */
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
static int phase, error, callback_fd = -1, recursive;

void constructor_step(int, int);
void constructor_record(char);
void constructor_preinit(void);
void constructor_initarray(void);
void constructor_finiarray(void);

void constructor_step(int before, int after) {
    if (phase != before) error = 1;
    phase = after;
}
void constructor_record(char c) {
    if (callback_fd >= 0 && write(callback_fd, &c, 1) != 1) _exit(1);
}
void constructor_preinit(void) {
    if (!environ || !getprogname() || !*getprogname()) error = 1;
    constructor_step(0, 1);
}
void constructor_initarray(void) { constructor_step(3, 6); }
void constructor_finiarray(void) { constructor_record('F'); }
static void mark_k(void) { constructor_record('K'); }
static void mark_e(void) { constructor_record('E'); }
static void mark_n(void) { constructor_record('N'); }

__attribute__((constructor(101))) static void early(void) {
    char *p = malloc(32);
    if (!p || snprintf(p, 32, "startup-%d", phase) != 9 || strcmp(p, "startup-1"))
        error = 1;
    free(p);
    constructor_step(1, 2);
    if (atexit(mark_k)) error = 1;
}
__attribute__((constructor(300))) static void later(void) { constructor_step(6, 4); }
__attribute__((constructor)) static void last(void) { constructor_step(4, 5); }
__attribute__((destructor)) static void first_fini(void) {
    constructor_record('D');
    if (recursive) exit(31);
}
__attribute__((destructor(300))) static void later_fini(void) { constructor_record('C'); }
__attribute__((destructor(101))) static void last_fini(void) {
    constructor_record('A');
    if (atexit(mark_n)) _exit(1);
}

static int check_exit(int mode) {
    int fds[2], status = 0;
    if (pipe(fds)) return 1;
    fflush(NULL);
    pid_t pid = fork();
    if (!pid) {
        close(fds[0]); callback_fd = fds[1]; recursive = mode == 2;
        if (atexit(mark_e)) _exit(1);
        if (mode == 1) _exit(23);
        exit(23);
    }
    close(fds[1]);
    char data[16]; size_t used = 0;
    for (;;) {
        ssize_t n = read(fds[0], data+used, sizeof(data)-used);
        if (n < 0) return 1;
        if (!n) break;
        used += n;
        if (used == sizeof(data)) return 1;
    }
    close(fds[0]);
    int failed = pid < 0 || waitpid(pid, &status, 0) != pid ||
        status != ((mode == 2 ? 31 : 23) << 8) ||
        (mode == 1 ? used != 0 : used != 8 || memcmp(data, "EKDCFBAN", 8));
    if (failed)
        fprintf(stderr, "exit mode=%d status=%d callbacks=%.*s bytes=%zu\n",
            mode, status, (int)used, data, used);
    return failed;
}
int main(void) {
    if (error || phase != 5 || check_exit(0) || check_exit(1) || check_exit(2)) {
        fprintf(stderr, "constructor/destructor failure: phase=%d error=%d\n", phase, error);
        return 165;
    }
    puts("NetBSD/RVM: preinit, constructor priorities, destructors, atexit and recursive exit verified");
    return 0;
}
