/* A partial FFS page must be writable through UBC without making user maps writable. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "native FFS failure at line %d errno=%d\n", __LINE__, errno); \
    return 1; } } while (0)

static void *fault_address;

static void
readonly_fault(int signal, siginfo_t *info, void *context)
{
    (void)context;
    _exit(signal == SIGSEGV && info->si_addr == fault_address ? 0 : 4);
}

int
main(void)
{
    enum { block = 16384 };
    char data[block], actual[block], byte;
    struct stat st;
    size_t page = getpagesize();
    CHECK(page == 65536);
    int fd = open("/ffs-partial-page", O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    /* Leave a hole in the first block and beyond EOF in the same VM page.
     * Before the fix this first pwrite loops in supervisor write faults. */
    memset(data, 'a', sizeof(data));
    CHECK(pwrite(fd, data, sizeof(data), block) == sizeof(data));
    CHECK(fsync(fd) == 0 && close(fd) == 0);
    fd = open("/ffs-partial-page", O_RDWR);
    CHECK(fd >= 0);
    CHECK(pread(fd, &byte, 1, 0) == 1 && byte == 0);
    CHECK(pread(fd, actual, sizeof(actual), block) == sizeof(actual));
    CHECK(memcmp(data, actual, sizeof(data)) == 0);
    char *mapping = mmap(NULL, page, PROT_READ, MAP_SHARED, fd, 0);
    CHECK(mapping != MAP_FAILED && mapping[block] == 'a');
    memset(data, 'b', sizeof(data));
    CHECK(pwrite(fd, data, sizeof(data), block) == sizeof(data));
    CHECK(fsync(fd) == 0 && mapping[block] == 'b');
    CHECK(fstat(fd, &st) == 0 && st.st_size == 2 * block);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_sigaction = readonly_fault;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        fault_address = mapping + block;
        if (sigaction(SIGSEGV, &action, NULL) != 0)
            _exit(2);
        *(volatile char *)fault_address = 'x';
        _exit(3);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && status == 0);
    CHECK(mapping[0] == 0 && mapping[block] == 'b');
    CHECK(munmap(mapping, page) == 0 && close(fd) == 0);
    CHECK(unlink("/ffs-partial-page") == 0);
    puts("RVM native FFS: partial-page sparse writes and read-only user mappings verified");
    return 0;
}
