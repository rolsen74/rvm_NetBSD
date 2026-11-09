/* Exercise tmpfs mappings, truncation faults and mount accounting with 64 KiB pages. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "native tmpfs failure at line %d errno=%d\n", __LINE__, errno); \
    return 1; } } while (0)

static void *fault_address;
static void
truncated_mapping(int signal, siginfo_t *info, void *context)
{
    (void)context;
    _exit(signal == SIGBUS && info->si_code == BUS_ADRERR &&
        info->si_addr == fault_address ? 0 : 2);
}

int
main(void)
{
    struct statvfs fs;
    CHECK(statvfs("/tmp", &fs) == 0 && strcmp(fs.f_fstypename, "tmpfs") == 0);
    struct stat st;
    CHECK(stat("/tmp", &st) == 0 && (st.st_mode & 07777) == 01777);
    size_t page = (size_t)sysconf(_SC_PAGESIZE), length = page * 3;
    int fd = open("/tmp/mapped", O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && ftruncate(fd, length) == 0);
    unsigned char *shared = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    CHECK(shared != MAP_FAILED);
    for (size_t i = 0; i < length; i++)
        CHECK(shared[i] == 0);
    shared[page-1] = 19;
    shared[page] = 23;
    shared[length-1] = 29;
    CHECK(msync(shared, length, MS_SYNC) == 0);
    unsigned char byte;
    CHECK(pread(fd, &byte, 1, page) == 1 && byte == 23);
    unsigned char *private = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
    CHECK(private != MAP_FAILED && private[page] == 23);
    private[page] = 31;
    CHECK(shared[page] == 23);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        shared[page] = 37;
        _exit(private[page] == 31 ? 0 : 1);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child && status == 0 && shared[page] == 37);
    CHECK(private[page] == 31 && munmap(private, length) == 0);
    CHECK(link("/tmp/mapped", "/tmp/linked") == 0 && unlink("/tmp/mapped") == 0);
    CHECK(stat("/tmp/linked", &st) == 0 && st.st_nlink == 1);
    CHECK(ftruncate(fd, page) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        struct sigaction action;
        memset(&action, 0, sizeof(action));
        action.sa_sigaction = truncated_mapping;
        action.sa_flags = SA_SIGINFO;
        sigemptyset(&action.sa_mask);
        fault_address = shared + 2 * page;
        if (sigaction(SIGBUS, &action, NULL) != 0 || sigaction(SIGSEGV, &action, NULL) != 0)
            _exit(4);
        volatile unsigned char beyond = shared[2 * page];
        (void)beyond;
        _exit(3);
    }
    CHECK(waitpid(child, &status, 0) == child && status == 0);
    CHECK(munmap(shared, length) == 0);
    CHECK(ftruncate(fd, length) == 0);
    CHECK(pread(fd, &byte, 1, page) == 1 && byte == 0);
    CHECK(unlink("/tmp/linked") == 0 && pwrite(fd, "x", 1, 0) == 1 && close(fd) == 0);
    puts("RVM native tmpfs: sparse files, shared/private mmap, fork, truncation SIGBUS and unlinked I/O verified");

    fd = open("/tmp/quota", O_RDWR | O_CREAT | O_EXCL, 0600);
    unsigned char *buffer = calloc(1, page);
    CHECK(fd >= 0 && buffer != NULL);
    size_t pages;
    for (pages = 0; pages < 256; pages++) {
        ssize_t result = write(fd, buffer, page);
        if (result < 0) {
            CHECK(errno == ENOSPC);
            break;
        }
        CHECK(result == (ssize_t)page);
    }
    CHECK(pages > 0 && pages < 256 && unlink("/tmp/quota") == 0 && close(fd) == 0);
    fd = open("/tmp/reclaimed", O_RDWR | O_CREAT | O_EXCL, 0600);
    CHECK(fd >= 0 && write(fd, buffer, page) == (ssize_t)page);
    CHECK(unlink("/tmp/reclaimed") == 0 && close(fd) == 0);
    free(buffer);
    puts("RVM native tmpfs: mount size limit and reclaimed capacity verified");
    return 0;
}
