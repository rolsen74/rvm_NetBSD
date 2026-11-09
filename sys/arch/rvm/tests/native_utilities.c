/* Exercise ordinary BSD-make utilities on a writable guest FFS mount. */
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "native utilities failure at line %d errno=%d\n", __LINE__, errno); \
    return 1; } } while (0)

static int
run(char *const args[], int expected)
{
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        execv(args[0], args);
        _exit(127);
    }
    int status;
    pid_t waited;
    do { waited = pid < 0 ? -1 : waitpid(pid, &status, 0); }
    while (waited < 0 && errno == EINTR);
    return pid > 0 && waited == pid && WIFEXITED(status) &&
        WEXITSTATUS(status) == expected;
}
#define RUN(status, ...) CHECK(run((char *const[]){__VA_ARGS__, NULL}, status))

int
main(void)
{
    char *device = NULL;
    CHECK(mount("ffs", "/", MNT_UPDATE, &device, sizeof(device)) == 0);
    RUN(0, "/bin/native-mkdir", "-p", "/tmp/native-files/a/b");
    unsigned char input[8193], output[8193];
    for (size_t i = 0; i < sizeof(input); i++)
        input[i] = (unsigned char)(i * 37);
    const char *source = "/tmp/native-files/a/source";
    int fd = open(source, O_WRONLY | O_CREAT | O_EXCL, 0640);
    CHECK(fd >= 0 && write(fd, input, sizeof(input)) == sizeof(input));
    CHECK(close(fd) == 0);
    CHECK(symlink("source", "/tmp/native-files/a/link") == 0);
    RUN(0, "/bin/native-test", "-f", source);
    RUN(1, "/bin/native-test", "-d", source);
    RUN(0, "/bin/native-cp", "-p", source, "/tmp/native-files/copy");
    fd = open("/tmp/native-files/copy", O_RDONLY);
    CHECK(fd >= 0 && read(fd, output, sizeof(output)) == sizeof(output));
    CHECK(memcmp(input, output, sizeof(input)) == 0 && close(fd) == 0);
    struct stat info;
    CHECK(stat("/tmp/native-files/copy", &info) == 0 &&
        (info.st_mode & 0777) == 0640 && info.st_size == sizeof(input));
    RUN(0, "/bin/native-cp", "-RP", "/tmp/native-files/a", "/tmp/native-files/tree");
    CHECK(lstat("/tmp/native-files/tree/link", &info) == 0 && S_ISLNK(info.st_mode));
    char link[16];
    CHECK(readlink("/tmp/native-files/tree/link", link, sizeof(link)) == 6 &&
        memcmp(link, "source", 6) == 0);
    RUN(0, "/bin/native-mv", "/tmp/native-files/copy", "/tmp/native-files/moved");
    CHECK(access("/tmp/native-files/copy", F_OK) == -1 && errno == ENOENT);
    RUN(0, "/bin/native-test", "-s", "/tmp/native-files/moved");
    RUN(0, "/bin/native-ls", "-lnhR", "/tmp/native-files");
    RUN(0, "/bin/native-rm", "-rf", "/tmp/native-files");
    CHECK(access("/tmp/native-files", F_OK) == -1 && errno == ENOENT);
    sync();
    CHECK(mount("ffs", "/", MNT_UPDATE | MNT_RDONLY, &device, sizeof(device)) == 0);
    puts("RVM native utilities: FFS mkdir, test, copy, recursive symlinks, rename, listing and removal verified");
    return 0;
}
