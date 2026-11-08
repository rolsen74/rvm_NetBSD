/* Native userland integration. Capture the real shell's output and status. */
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
extern char **environ;
int native_check(void);
int native_check(void) {
    int fds[2], status = 0;
    if (pipe(fds)) return 161;
    fflush(NULL);
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        if (dup2(fds[1], 1) < 0) _exit(1);
        close(fds[1]);
        char *args[] = { "/bin/sh", "/etc/sh-check", NULL };
        execve(args[0], args, environ);
        fprintf(stderr, "shell exec failed: errno=%d\n", errno);
        _exit(127);
    }
    close(fds[1]);
    char output[512]; size_t done = 0;
    for (;;) {
        ssize_t n = read(fds[0], output + done, sizeof(output) - 1 - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        done += n;
        if (done == sizeof(output) - 1) break;
    }
    output[done] = '\0';
    close(fds[0]);
    if (pid < 0 || waitpid(pid, &status, 0) != pid || status != 0 ||
        !strstr(output, "native utilities and sh quoting,")) {
        fprintf(stderr, "native userland failure: status=%d output=%s\n", status, output);
        return 161;
    }
    fputs(output, stdout);
    const char *files[] = { "/tmp/sh-output", "/tmp/sh-error", "/tmp/sh-background", "/tmp/sh-exec", "/tmp/sh-list" };
    for (unsigned i = 0; i < sizeof(files)/sizeof(files[0]); i++)
        if (unlink(files[i])) return 161;
    return 0;
}
